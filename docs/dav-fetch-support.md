# WebDAV bulk content fetch (REPORT)

## Problem

`WebdavThread::ReadDirectory()` in `scribe/trunk_os/src/Store3Webdav` fetches a folder with one `PROPFIND`, then queues one load command per calendar or contact file and the thread loop performs one `GET` for each. A folder with 300 items costs 301 requests, and because `LWebdav::Request()` calls `GetSocket()` for every request each one is a fresh TLS connection, so the cost is 300 TLS handshakes as well as 300 HTTP round trips.

Refresh repeats the same work: the listing is re-read and every file is fetched again even when nothing changed on the server.

## Design

Use the RFC 3258 `REPORT` method with the `calendar-multiget` / `addressbook-multiget` report types, which let the client name the resources it wants the content for in a single request. Scribe keeps the existing per-file `GET` path as the fallback for servers that do not implement `REPORT`.

This functionality is triggered by setting up a webdav account:
    - File → Manage Mail Stores, then double-click the store's location column or context-menu Edit on a WebDAV row (ManageMailStores.cpp:203-238), or add a new one via the "Webdav Remote Folder" item (ManageMailStores.cpp:393, 419).

### Documentation for users

Under 'scribe/trunk_os/help' there needs to be a new page describing how to set up webdav folders via the File -> Manage Mail Stores dialog. That needs to be accessible from the menu.html and a note added to the calendar and contact sections so that the user is aware of the feature.

### Capability gate

`LWebdav::SupportsReport(resource)` performs a single `OPTIONS` and caches the answer in `ReportChecked` / `ReportOk` on the client object. `REPORT` is only used when the server lists it in the `Allow` header. The client must not assume CalDAV or CardDAV support from the folder type alone, because a plain WebDAV server can host the same files and will answer `REPORT` with an error.

### Request shape

`LWebdav::Report(files, resource, report_type, hrefs)` builds the multiget body:

- `prop` asks for `getetag` plus the content element, `calendar-data` for `calendar-multiget` (`urn:ietf:params:xml:ns:caldav`) or `address-data` for `addressbook-multiget` (`urn:ietf:params:xml:ns:vcard-4.0`).
- one `href` per resource, using the full path as the server reported it, XML escaped.
- hrefs are sent in batches of 100 so a large folder does not produce a request body a server will reject.
- both `200` and `207` are accepted as success, since servers differ on whether a multiget response is a multistatus.
- cancellation is honoured between batches.

The report type is chosen from the folder field (`FIELD_FOLDER_TYPE`), not from the href extension.

### Response parsing

`PROPFIND` and `REPORT` share `ParseMultistatus(root, files, data_tag)`:

- elements are matched by local name rather than a literal `d:` prefix, because servers use arbitrary namespace prefixes; `LFindXml()` matches on local name and `XmlNameIs()` / `XmlChild()` do the same for direct children.
- a `propstat` whose status is not successful is skipped, so a resource that is listed but whose requested properties were refused does not produce a half-populated entry.
- an entry is recorded when it has `getcontentlength` or the requested content element; a multiget response with content only is still recorded.
- hrefs are normalised to a path relative to the endpoint (`RelativeHref()`), accepting both the path form and the absolute URI form servers may return, and the request side rebuilds the full path (`FullHref()`).

`FileProps::GetContentType()` is const so the listing can be inspected from const references.

### Change detection in the thread

`ReadDirectory()` no longer loads everything unconditionally:

1. `PROPFIND` the collection to get the current listing.
2. Snapshot `Files` into a local `Known` array while holding the thread mutex.
3. For each listed item that is a calendar or contact (`IsItem()`), compare the reported etag with the stored etag. An item is unchanged only when it has stored content *and* matching etags; items with no stored content are re-requested so a previously failed fetch is retried.
4. Build the href list for the items needing content, call `Report()` once, and merge the returned content into the new listing. Anything the bulk response did not return is fetched with a single `GET` for that file only.
5. Post `CmdFile` events for items whose content was fetched or changed, and `CmdDelete` events for items the server no longer lists.
6. Publish the new listing to `Files` under the lock.

Events are posted before `Files` is replaced, and the items needing content are referenced by index into the new listing, so the fetch must not mutate that array while the events are being built.

Network I/O stays outside the thread mutex. Only `Files` and `Cmds` are touched under the lock, as before.

### Event handling

`WebdavStore::OnEvent()` must not assume every `CmdFile` is a new item. It looks for an existing item by href and, when found, updates it through `WebdavObj::SetData()` and fires `FireOnChange()`; only an unknown href creates a `WebdavCalendar` or `WebdavContact` and calls `Callback->OnNew()`. Without this, change detection produces duplicate items in the folder view.

`WebdavObj::GetHref()` is the accessor used for that lookup, which requires the base `Href` member to be the one that is populated. `WebdavCalendar` declared its own `Href` that shadowed `WebdavObj::Href`, so items created from events never had a usable href; that duplicate member was removed.

## Invariants

- With `REPORT` available, a folder load costs one `OPTIONS`, one `PROPFIND`, and one request per batch of needed files — never one request per file.
- A refresh with no server-side changes performs no content requests and posts no events.
- Content posted for a changed item comes from the current response, never from stale cached content.
- An item is represented by one object in the folder; content updates do not create a second object.
- Servers without `REPORT` behave as they did before, fetching each file individually.
- Failed or refused properties are skipped rather than recorded as content.

## Limitations and follow-up

- Change detection is etag-only. There is no RFC 6578 `sync-collection` support, so a refresh still lists the whole collection; sync tokens would remove that as well as reduce the multiset to what actually changed.
- Batch size is a fixed 100 hrefs. Servers may cap request size or the number of hrefs accepted; a rejected batch currently stops the bulk fetch and the remaining files fall back to individual requests.
- Connection reuse is unchanged: every `Request()` still opens a new TLS socket, so the fallback path costs one handshake per file. Reusing the socket would matter most for servers without `REPORT`.
- `IsItem()` relies on the reported content type, falling back to the folder extension when a server does not report it.
- Weak etags, quoted etags and servers that omit `getetag` in multiget responses cannot be detected as changed reliably; such servers will refetch content on every refresh.
- The `Depth` header on a multiget request is not required by the report and is sent only for compatibility.

## Acceptance checks

- Against a conformant CalDAV/CardDAV server, loading a large folder issues `OPTIONS`, `PROPFIND` and `REPORT` and no per-file `GET`.
- A second refresh with nothing changed issues only the listing request and no content requests.
- Changing one file on the server updates the existing item in the view, not a duplicate.
- Deleting a file on the server produces a delete event and removes the item.
- Against a server whose `OPTIONS` does not list `REPORT`, behaviour and request counts match the previous implementation.
- Cancelling a folder fetch between batches stops without additional requests.
