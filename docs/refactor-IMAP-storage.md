# IMAP Mail Store backend storage

## Existing implementation

Currently the IMAP Store3 implementation that lives in 'scribe/trunk_os/src/Store3Imap' uses the filesystem to store it's local cache of folder indexes (as .xml) and email (as .eml files). This is probably NOT ideal and dupelicates all the functionality in the mail3 storage implementation in 'scribe/trunk_os/src/Store3Mail3' which uses an Sqlite3 database as it's permanent persistance.

It makes sense to me to move the IMAP implementation to using the SAME Sqlite3 database as the Mail3 store. There are several benefits:
    - moving email between a Mail3 folder and an IMAP folder just reparents the Mail record to a new folder rather than making an expensive copy between 2 separate backends. Obviously the IMAP server will need to get involved to propagate that change to there.
    - I would like to be able to speed up searching using the Sqlite full text search functionality such that it works for IMAP email too. So bringing all the IMAP email into the Sqlite database could enable that.

However there are a lot of risks too. Things like threading and ownership of data causing difficult to track down bugs. Added complexity to APIs making maintainence work harder. Or new performance issues.

## Plan for merging the backends to use the same Sqlite database

### Implementation progress

IMAP mailbox and message state now lives directly in the canonical `Folder` and `Mail` tables, using a union of local and IMAP fields. The ordinary Mail3 schema upgrade path adds the nullable columns, with uniqueness indexes for mailbox identity and UID scope. There are no separate `ImapMailbox` or `ImapMessage` tables, and no migration from those unused experimental tables. `LMail3Store` exposes typed mailbox and message-state load/save/clear methods, guarded by its existing owning-thread rule. Persistent IMAP account connections receive a non-owning reference to the already-loaded default Mail3 backend; one-off IMAP connections remain unchanged.

The rollout now rebuilds staged SQLite mailbox contents from the server instead of importing legacy cached messages. An opt-in server refresh captures UIDVALIDITY/UIDNEXT from SELECT and fetches complete UID/flag/header listings. The old cache supplies only local customizations: labels, colours and folder display settings. This is still staging, not a live-storage cutover: the IMAP UI/runtime continues to use its existing filesystem backend. Canonical-object adapters, live SQLite body downloads and operation-journal execution remain to be implemented.

Use the existing `LMail3Store` database as the single local source of truth for both Mail3 and IMAP mail. Keep the IMAP protocol engine and its `LDataStoreI`-facing behavior, but make it a protocol adapter over the shared store rather than a second owner of cached messages and folders.

Reuse the existing `Folder`, `Mail`, and `MailSegs` records for the local folder tree, message properties, and MIME content. A message has one local mail ID and one parent folder; moving it between local folders updates its parent reference instead of copying its MIME tree. The shared schema is:

- `Folder`: adds `ImapAccountId`, `ImapRemoteName`, `ImapDelimiter`, `ImapUidValidity`, `ImapUidNext` and `ImapHighestModSeq`. `Folder.Id` is also the mailbox identity; there is no duplicated `FolderId`. Enforce uniqueness on `(ImapAccountId, ImapRemoteName)`.
- `Mail`: adds `ImapUidValidity`, `ImapUid`, `ImapRemoteFlags`, `ImapLocalFlags`, `ImapModSeq` and `ImapDownloaded`. Existing `AccountId` identifies the account and `ParentId` identifies the canonical mailbox folder. Enforce uniqueness on `(ParentId, ImapUidValidity, ImapUid)`; no separate `MailId` or `MailboxId` columns are needed. Clearing IMAP state leaves the canonical message and MIME content intact.
- `MailSegs`: remains the canonical MIME storage, keyed to `Mail.Id`.
- `ImapOperation`: remains a separate durable journal for operations that must be applied to the server, including cross-folder moves, appends, flag updates and deletes. Source/destination mailbox IDs refer directly to canonical `Folder.Id` values. Store enough state to retry and reconcile after a restart.
- `ImapLocalSettings`: a recovery snapshot keyed by account and remote mailbox name. Contains only folder display settings and Message-ID/label/colour records, including duplicates so ambiguity is not lost. It contains no cached UIDs, server flags, bodies or download state.

IMAP columns remain NULL for local-only folders/messages. Normal canonical object serialization preserves them during ordinary load/save; copies into local storage do not inherit server identities. Mod-sequences are decimal text so the full unsigned 64-bit range is retained. The typed mailbox/message structures are access views of these canonical rows, not separate persisted identities. Recovery snapshots stay separate because they must retain unmatched settings even after the original folder or message disappears.

Further synchronization fields should follow the IMAP commands and state already used by `ImapThread`; do not treat a UID as globally unique or valid across UIDVALIDITY changes. Keep account identity explicit even if the current application account ID is used as its key.

### Ownership and data flow

`LMail3Store` owns the SQLite connection, schema upgrades, transactions, canonical folder/mail objects and MIME BLOB streams. The IMAP adapter receives a reference to that store and maps protocol events directly to canonical folder/mail rows and their IMAP fields. It must not open a second SQLite connection to the same database.

The IMAP network worker continues to perform network I/O and post results to the store-owning thread. Database and canonical object mutations happen on the owning thread, consistent with `LMail3Store`'s existing thread affinity. Use existing store transactions for each local state transition. UI notifications are emitted only after the corresponding transaction commits.

Keep the existing IMAP object/API boundary initially. `ImapMail` and `ImapFolder` can act as adapters or views over canonical objects while callers are migrated; avoid making the network thread own shared `LMail3Mail` instances. In particular, preserve delayed-operation status, folder loading behavior, and event ordering during the transition.

### IMAP synchronization and moves

An SQLite transaction cannot include an IMAP server command. Do not implement cross-boundary moves as a bare local `ParentId` update. Record the requested operation durably, apply it to the server, then reconcile the resulting UID/flags and local parent in a transaction. If the UI needs optimistic moves, represent that pending state explicitly and retain enough source mapping to recover or roll back after failure. Retries must be idempotent, and reconciliation must account for servers without the IMAP MOVE extension (for example, COPY followed by delete/expunge).

For a move entirely within the local Mail3 store, update the existing mail row's parent inside a transaction; its MIME BLOBs remain unchanged. For a move involving an IMAP mailbox, keep the `Mail.Id` and MIME content where possible, but update or replace the IMAP mapping only when the server confirms the destination identity. Moving from a Mail3-only folder to IMAP requires an APPEND and creation of a new server UID mapping; moving from IMAP to a Mail3-only folder requires the configured server-side delete policy to be completed or recorded as pending.

Retain the original RFC822 bytes for IMAP APPEND/retry where re-encoding the MIME tree could change message content. The staging importer uses Mail3's existing flat segment representation and verifies the reconstructed bytes. The live adapter must preserve that representation until any pending operations no longer need the original image.

### Search

Add an SQLite FTS index for searchable text from both Mail3 and IMAP mail, indexed by canonical mail ID. Index headers/body text only, never attachment binary data. Keep the FTS row synchronized with message creation, update, move and deletion in the same transaction where supported; moving a message should not require reindexing its content. Check that the SQLite build used by all supported targets includes the selected FTS module, and provide a rebuild path for the derived index.

### Migration and rollout

#### Current server-backed staging rebuild

Start Scribe with `--imap-rebuild-cache` and connect a persistent IMAP account to rebuild its SQLite metadata in the already-loaded default Mail3 store. The retired `--imap-import-cache` option reports an error rather than silently importing legacy identities. Normal connections without either option and one-off IMAP connections are unchanged.

Before starting the worker, `ImportImapSettings` captures a per-mailbox recovery snapshot from legacy XML. It retains labels, colours, sort order, expanded/threaded state and display-column IDs/widths, but not unread counts, access permissions or other server state. It does not read cached message bodies or import folder/message membership. Recovery is atomic and existing snapshots are not overwritten on reconnect. Unsupported Protobuf indexes, unreadable sources and malformed XML cause explicit failure. A missing legacy cache is fine.

The worker lists the server's folders and queues a full header refresh for each selectable mailbox, regardless of the legacy last UID. SELECT is forced before and after FETCH: UIDVALIDITY, UIDNEXT and EXISTS must remain unchanged, the returned message count must match EXISTS, and all messages must have valid unique UIDs and headers before anything is committed. Empty mailboxes are handled without issuing an invalid FETCH. Incomplete/changing listings are reported as failures; retry by reconnecting. No cached server identity is used to build the SQLite snapshot.

`RebuildImapMailbox` creates the server-confirmed folder hierarchy under `IMAP (<AccountId>)`, applying recovered display settings when creating folders. A complete mailbox snapshot is committed on the Mail3 owning thread in one savepoint. It replaces stale identities, retains canonical IDs/content for surviving UIDs in the same epoch, removes obsolete membership and stores the verified SELECT epoch. New identities remain `ImapDownloaded=0`, with no fabricated MIME content. A changed UIDVALIDITY replaces old identities and cached bodies rather than reusing old UIDs. Accounts with operation-journal records are refused until those records are reconciled. A failed refresh rolls back without damaging the prior snapshot or unrelated local mail.

Labels and colours are restored **only when Message-ID matches exactly (after trimming surrounding whitespace) and occurs once in both the source and the complete destination mailbox listing**. Matching is scoped to one account and mailbox; there is no UID, subject/date, cross-folder or cross-account fallback. Missing, duplicate and unmatched IDs are retained in the recovery snapshot and legacy cache, not guessed. Subsequent refreshes preserve local edits on surviving identities; current SQLite customizations are saved back into the recovery snapshot transactionally before obsolete identities are removed. This also preserves recoverable local edits across epoch resets and temporarily missing server messages, rather than reverting to the original cache's settings.

The recovery/rebuild methods never rewrite or delete the legacy cache, including pending uploads and unindexed files. The still-active legacy backend can continue its normal cache writes. Local-only uploads are not uploaded automatically by this staged rebuild and must be recovered before eventually retiring that backend. The older raw-cache import API remains available for existing recovery tests, but is no longer wired into account startup.

Staged folders/messages do not replace the live IMAP UI objects or emit normal new-mail notifications. Reload the default store to inspect the resulting SQLite subtree after staging completes; do not edit that subtree concurrently with a rebuild. This step does not yet provide SQLite-backed on-demand body downloads, folder-removal reconciliation for the whole account, or a complete live storage cutover.

Native fixtures exercise SELECT parsing, verified server identities, unique/ambiguous Message-ID recovery, folder settings, account/mailbox isolation, restart/idempotency, epoch resets, empty mailboxes and injected transaction failures, in addition to the older raw-message recovery tests. Canonical-schema fixtures also check local-only NULL fields, uniqueness, normal object serialization, clearing identities without losing content, and upgrading existing Mail3 folders/messages/bodies. On Linux, from the repository root with the normal debug dependencies built:

```sh
GDK_BACKEND=x11 xvfb-run -a make -f unittests/Makefile.imap-storage -j2 imap-storage-test
```

#### Remaining rollout

1. Keep canonical IMAP fields and journal/recovery tables on the existing Mail3 database upgrade path. Complete rollout/recovery validation across supported platforms; never discard legacy cache files during an upgrade.
2. Capture only local customizations from the old cache. Rebuild folder/message membership, UIDs, epochs, flags and metadata from the server; download bodies from the server on demand. Do not reconcile legacy UIDs or import old bodies as authoritative.
3. Verify server-derived folder/message counts and identities, conservative customization restoration, restart and raw-message fidelity. Preserve the old cache as a recovery source for unmatched customizations and local-only uploads.
4. Switch IMAP reads and writes to the shared database, with one authoritative write path. Avoid prolonged dual writes; they create two conflicting sources of truth.
5. Add and validate FTS indexing/rebuild behavior independently from the storage cutover.
6. Remove legacy XML/Protobuf folder indexes and `.eml` cache writes only after upgrade, restart, retry and rollback behavior has been exercised. Retain a deliberate recovery/backup policy for users upgrading from older versions.

### Invariants and acceptance checks

- Every locally cached message has one canonical mail row and one parent folder; its MIME segments are not duplicated by a local move.
- Every IMAP message stores its server identity on its canonical mail row, in the correct account/parent-folder/UIDVALIDITY scope.
- A message that is not fully downloaded can retain its IMAP metadata without pretending that its RFC822 content is available locally.
- Local database commits and UI notifications do not claim server success before the server operation has been confirmed.
- Folder synchronization, reconnect, expunge, UIDVALIDITY reset and operation retry preserve or explicitly reconcile local/server state.
- Existing Mail3-only storage, folders, MIME streaming, backup/restore and schema upgrades continue to work.
- FTS results identify canonical mail IDs and remain correct after updates, deletes and moves.

### Open design checks before implementation

- Confirm whether one `LMail3Store` instance/database is available to every IMAP account that should share the Mail3 store, and define how that instance is injected and kept alive.
- Confirm SQLite thread/connection constraints and transaction ownership around IMAP callbacks.
- Inventory the complete IMAP sync state currently stored in folder metadata and per-message XML/protobuf before finalizing the shared schema.
- Decide the user-visible failure policy for server-backed moves and deletes, including offline operation and retries.
- Verify raw RFC822 fidelity and FTS availability across Linux, macOS, Windows and other supported builds.