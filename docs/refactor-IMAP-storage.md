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

Persistent accounts now use SQLite-backed `ImapFolder`/`ImapMail` adapters as their normal live backend, with no opt-in switch and no filesystem fallback. Server refresh captures UIDVALIDITY/UIDNEXT from SELECT and fetches complete UID/flag/header listings. The old cache supplies only local customizations: labels, colours and folder display settings. Local-only legacy messages are recovered separately into ordinary local Mail3 folders, never automatically uploaded.

The live adapters read canonical metadata and bodies, commit verified downloads on the owning thread, and journal flag/delete/same-account move/APPEND requests. Read-only reconciliation and explicit continuation of partially completed moves with recorded COPYUID evidence use the same bridge, described below. These paths are tested against the real SQLite/Mail3 implementation and scripted `MailIMap` transport. The full feature is not finished: durable folder operations, cross-store/cross-account transfers, complete account-folder inventory reconciliation and recovery-decision UX remain outstanding. Unsupported operations fail explicitly instead of using the legacy filesystem path.

### Live adapter behavior

`ImapFolder` and `ImapMail` own detached canonical views; the existing default Mail3 store still owns the one SQLite connection. Folder columns, sort/thread/expanded settings and message labels/colours/priority persist through canonical serialization. Normal local Mail3 navigation hides the IMAP subtree, and direct local move/delete/change/save paths reject IMAP records rather than bypassing the protocol journal.

The worker posts a detached selectable-mailbox inventory to the owning thread. That thread queues complete snapshots with the current mutation generation and server delimiter, including after reconnect. A snapshot predating a queued mutation is discarded, and account refreshes wait until the journal drains. Membership and server flags change in the adapters after canonical commit, not when a command is merely posted. Pending move/delete state is recovered from the journal when wrappers reload.

On-demand downloads carry the canonical mail ID, mailbox ID, UID and UIDVALIDITY. The worker forces SELECT before and after `UID FETCH ... BODY.PEEK[]`, requires exactly one matching body and the expected RFC822 byte length, and posts detached binary-safe bytes. The owning thread revalidates identity before storing the body and notifying the UI; failure also clears the loading indication and reports a diagnostic. SQLite-backed RFC822 streams support offline reading after restart. Header/body/timestamp edits to an existing server message are rejected rather than silently diverging from the server; a replacement requires a new APPEND.

New APPEND requests atomically preserve the original RFC822 image, label/colour/priority and flags with durable intent. Flag changes, deletes and same-account moves use the ordered journal; bulk deletion also uses individual durable operations and never sends an unjournaled EXPUNGE. Ambiguous results stop automatic dispatch and retain their evidence. Folder create/delete/rename/expunge are currently unavailable, as are complete recovery controls and shared-ID transfers between local storage and IMAP or between accounts.

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

Retain the original RFC822 bytes for IMAP APPEND/retry where re-encoding the MIME tree could change message content. APPEND staging and local-mail recovery use Mail3's existing flat segment representation and verify the reconstructed bytes. The live adapter preserves that representation while pending operations need the original image.

#### Read/write cutover persistence and internal dispatch

`StoreImapBody` accepts a canonical mail ID, parent-folder ID, account, UIDVALIDITY and UID. It rechecks those identities against both canonical tables, verifies RFC822 size and Message-ID, stores the original bytes in `MailSegs`, verifies byte-for-byte readback and marks the message downloaded in one savepoint. A stale identity, incomplete input or late write failure rolls back the entire replacement, preserving any prior body. `OpenImapBody` exposes the stored RFC822 stream only for the same verified identity and an actually downloaded flat MIME image; it never invents empty content for a metadata-only message. The network adapter must additionally verify the server's selected UIDVALIDITY before supplying a download.

`StageImapAppend` stores a new canonical message and its original RFC822 bytes together with an APPEND journal entry in one transaction. The message has no speculative UID. No temporary `.eml` is required by this API, and the upload image survives restart. New live-adapter saves use this path; one-off connections retain their legacy implementation.

The typed message-operation journal covers flag replacement, delete, move and APPEND. Each entry freezes the canonical mail ID, source folder/name/epoch/UID, destination folder/name/epoch where applicable, desired flags and attempt count. A unique index permits only one outstanding operation per mail row. Queuing and claiming require an idle owning-thread connection so a network handoff cannot precede the durable commit:

1. `QueueImapOperation` persists intent without claiming server success or changing canonical membership/flags. APPEND normally uses `StageImapAppend` instead.
2. `BeginImapOperation` revalidates the frozen identities and durably changes `Queued` to `Executing` before dispatch. Work is ordered per account; an unresolved earlier entry blocks later commands for that account, not unrelated accounts.
3. `CompleteImapOperation` is called only after server confirmation. It atomically updates the canonical row and removes the journal entry. COPY/APPEND require a confirmed destination UID in the expected epoch. A move preserves the canonical ID, local customizations and MIME content. Delete marks the message deleted but retains its content/identity until expunge or authoritative reconciliation.
4. `InterruptImapOperation` records an uncertain result and its diagnostic as `NeedsReconciliation`. `RecoverImapOperations` similarly quarantines executing entries after restart; untouched queued entries remain queued. Neither method retries an uncertain APPEND/COPY, discards the upload image, or silently retires the operation.

`ImapStore::DispatchJournalOperation` is the internal owning-thread bridge for a specifically selected queued operation. It checks the account's ordered queue and commits the claim before posting `IMAP_JOURNAL_OPERATION` to the network worker. APPEND preparation copies and verifies the upload into an owned `LTempStream` (which can spill to temporary storage for large messages) before dispatch; neither SQLite BLOB handles nor canonical objects cross to the worker. The durable upload remains in `MailSegs`, not a legacy `.eml` file. The prepared stream can survive closing the original database connection.

`ExecuteImapJournalOperation` forces SELECT and checks the frozen UIDVALIDITY, then fetches the source UID before any mutation. It verifies the resulting UID/flags/epoch after STORE. APPEND uses a new exact-length literal API without newline conversion or `strlen` on message data. COPY uses the shared `MailIMap::CopyByUid` API with an optional UIDPLUS result; existing batch callers omit it. That result contains a mapping only for a single requested UID, never a fabricated single-message identity for a batch. COPY/APPEND accept destination identity only from the matching tagged single-message COPYUID/APPENDUID response, with strict unsigned bounds. A move does not delete its source unless COPY returned a valid mapping and the destination message was verified; it then rechecks the source before marking it deleted. No Message-ID search or RECENT-based destination guessing is used.

The worker returns one of three explicit outcomes: `NotExecuted` (preflight failed before a mutation), `Uncertain` (a mutation was attempted but cannot be fully confirmed), or `Confirmed`. Rejected mutation commands are conservatively uncertain because the current protocol APIs do not distinguish every server rejection from transport loss. Missing/malformed UIDPLUS, partial uploads, missing messages, mismatched flags or changed epochs never become success-shaped results. A failed journal command discards its protocol connection so a partial/lost reply cannot contaminate the next command.

On the store thread, results are correlated to the account and executing journal entry. Confirmed commands use the existing atomic completion API. Other outcomes retain the journal entry and diagnostic for reconciliation; a valid COPYUID is persisted even if source deletion later fails. If the server succeeded but the local transaction fails, the bridge also quarantines the operation with its destination evidence instead of retrying COPY/APPEND. Duplicate or cross-account results are rejected. Taking a worker offline or destroying it with a command in flight quarantines that operation; after a process crash, explicit journal recovery still handles the lost response.

#### Read-only journal reconciliation

Passing `LMail3ImapOperationState::Reconciling` to `DispatchJournalOperation` (or the preparation helper used by native tests) selects the account's first `NeedsReconciliation` entry. It durably claims the distinct `Reconciling` state before posting to the same worker. The canonical message and frozen mailbox identities must still match. The default claim state remains `Executing` for ordinary queued work; invalid claim states are rejected. No APPEND payload is detached for recovery, and read-only reconciliation sends only SELECT/FETCH commands, never COPY, APPEND, STORE or EXPUNGE.

For flags/deletes, reconciliation requires the original source UID in the original epoch to have exactly the requested writable flags. For APPEND, it requires the previously recorded APPENDUID destination, its frozen epoch and the requested flags. For moves, it requires the recorded COPYUID destination with the original copied flags, plus a still-present source UID marked deleted in its original epoch. SELECT is forced before and after each FETCH; moves also recheck the destination UID, flags and epoch after examining the source. Verified operations complete through the same atomic canonical update/journal retirement used for live command results, preserving mail IDs, bodies and local customizations.

Missing UIDPLUS evidence, missing messages (including expunged sources), changed epochs/flags, duplicate/incomplete FETCH results and failed transport leave the operation quarantined with a diagnostic. There is deliberately no Message-ID/RECENT search, inferred destination UID, server mutation or automatic retry. A partial move whose source is not yet deleted remains blocked rather than repeating COPY or silently finishing source deletion. Lost APPEND/COPY replies without durably recorded destination evidence still require future explicit recovery/decision UX.

Every dispatch/reconciliation claim increments the journal attempt number. Results must match account, operation, active state and attempt; late responses from an earlier mutation or reconciliation cannot retire a newer claim. Reconciliation cannot substitute destination identities for the recorded UIDPLUS evidence. A failed local completion preserves that evidence and returns the operation to `NeedsReconciliation`, allowing another read-only verification. Going offline or restarting also quarantines an interrupted `Reconciling` claim.

Native tests cover all four operation types, restart and durable claims, stale/cross-account/duplicate responses, missing UIDPLUS evidence, missing/duplicate/incomplete FETCH data, changed epochs/flags and injected local commit failures. They assert that reconciliation sends no server mutations and preserves the original bodies, labels and colours.

#### Explicit partial-move continuation

Passing `LMail3ImapOperationState::ContinuingMove` to the same dispatch/preparation APIs explicitly authorizes finishing source deletion for a quarantined move. Claiming requires a valid, durably recorded COPYUID destination and unchanged canonical/frozen identities; it rejects APPEND, other operation types and unknown destinations before dispatch. The `ContinuingMove` state and incremented attempt number commit before the worker runs. This is not an automatic retry, and read-only reconciliation still never sends mutations.

The worker verifies the recorded destination UID, copied flags and frozen epoch, then the source UID and epoch. An already-deleted source needs no STORE. Otherwise its writable flags must still match the original copy snapshot before the worker issues a single `UID STORE <source> +FLAGS (\Deleted)`. The additive option on `MailIMap::SetFlagsByUid` preserves other system flags and server keywords, including concurrent flag edits; existing callers still replace flags by default. No COPY, APPEND or EXPUNGE is sent. After STORE, the source must be verified deleted in the same epoch; the destination UID, flags and epoch are checked again before canonical completion. These checks do not make operations across two server mailboxes atomic: concurrent changes can still require later synchronization.

Missing messages, changed flags/epochs, incomplete replies and failed STORE leave the move quarantined with the same COPYUID evidence and diagnostic. Lost STORE replies are uncertain, not permission to repeat COPY. After restart or local commit failure, read-only reconciliation can finish if deletion already happened; an explicit continuation can also verify that state without another STORE. A rejected source deletion can be explicitly continued again after re-verification. Interrupted continuation claims are quarantined by offline/crash recovery, and late results are fenced by active state and attempt just like other commands.

Native tests assert exact additive STORE bytes with unsigned UIDs, zero duplicate COPY/APPEND, preservation of keywords/concurrent flags, already-deleted handling, identity/flag/transport failures before and after STORE, durable claims and account ordering, missing-evidence/type rejection, restart/stale-response fencing and local commit rollback. Bodies, labels, colours and folder sort settings survive both success and failure.

Live message mutations call this bridge automatically after committing intent. There are no automatic retries of quarantined operations or legacy-cache notifications on journal completion. Confirmed results reload canonical membership and emit committed adapter notifications; when the ordered queue drains, authoritative snapshots are requested again. Recovery decisions for insufficient evidence, cancellation/retry/continuation UX and folder-operation journaling still need implementation. Mailbox names that cannot safely pass the existing quoted-path encoder are explicitly rejected by the dispatcher rather than sent with broken quoting.

### Search

Add an SQLite FTS index for searchable text from both Mail3 and IMAP mail, indexed by canonical mail ID. Index headers/body text only, never attachment binary data. Keep the FTS row synchronized with message creation, update, move and deletion in the same transaction where supported; moving a message should not require reindexing its content. Check that the SQLite build used by all supported targets includes the selected FTS module, and provide a rebuild path for the derived index.

### Migration and rollout

#### Default server-backed refresh and legacy recovery

Connect a persistent IMAP account normally: it uses the already-loaded default Mail3 store without a switch. A missing shared store is an explicit connection failure, not permission to fall back to the old cache. The retired `--imap-import-cache` option reports an error rather than silently importing legacy identities. One-off IMAP connections remain unchanged.

Before starting the worker, `ImportImapSettings` captures a per-mailbox recovery snapshot from legacy XML. It retains labels, colours, sort order, expanded/threaded state and display-column IDs/widths, but not unread counts, access permissions or other server state. It does not read cached message bodies or import folder/message membership. Recovery is atomic and existing snapshots are not overwritten on reconnect. Unsupported Protobuf indexes, unreadable sources and malformed XML cause explicit failure. A missing legacy cache is fine.

The worker lists the server's folders and queues a full header refresh for each selectable mailbox, regardless of the legacy last UID. SELECT is forced before and after FETCH: UIDVALIDITY, UIDNEXT and EXISTS must remain unchanged, the returned message count must match EXISTS, and all messages must have valid unique UIDs and headers before anything is committed. Empty mailboxes are handled without issuing an invalid FETCH. Incomplete/changing listings are reported as failures; retry by reconnecting. No cached server identity is used to build the SQLite snapshot.

`RebuildImapMailbox` creates the server-confirmed folder hierarchy under `IMAP (<AccountId>)`, applying recovered display settings when creating folders. A complete mailbox snapshot is committed on the Mail3 owning thread in one savepoint. It replaces stale identities, retains canonical IDs/content for surviving UIDs in the same epoch, removes obsolete membership and stores the verified SELECT epoch. New identities remain `ImapDownloaded=0`, with no fabricated MIME content. A changed UIDVALIDITY replaces old identities and cached bodies rather than reusing old UIDs. Accounts with operation-journal records are refused until those records are reconciled. A failed refresh rolls back without damaging the prior snapshot or unrelated local mail.

Labels and colours are restored **only when Message-ID matches exactly (after trimming surrounding whitespace) and occurs once in both the source and the complete destination mailbox listing**. Matching is scoped to one account and mailbox; there is no UID, subject/date, cross-folder or cross-account fallback. Missing, duplicate and unmatched IDs are retained in the recovery snapshot and legacy cache, not guessed. Subsequent refreshes preserve local edits on surviving identities; current SQLite customizations are saved back into the recovery snapshot transactionally before obsolete identities are removed. This also preserves recoverable local edits across epoch resets and temporarily missing server messages, rather than reverting to the original cache's settings.

The recovery/rebuild methods never rewrite or delete the legacy cache. Before starting the worker, `RecoverImapLocalMail` preserves pending/local-only RFC822 files in a normal `Recovered IMAP uploads (<AccountId>)` folder. Files referenced by a valid positive-UID legacy index entry are treated only as old server cache and excluded. All other `.eml` files are conservatively recovered, including UID-zero entries, temporary uploads and unindexed files. Indexed pending-message labels and colours are retained. Recovery verifies stored bytes, creates no IMAP identities or operation-journal entries, and updates already-loaded local folder views only after commit.

`ImapRecoveredFile` records account/source-path/mail-ID evidence with a unique account/path index. Unchanged files are skipped after restart; changed files are preserved as additional local mail before updating the record. If the previous recovered mail was deleted, the untouched source can be recovered again. Malformed indexes, unsafe filenames, symlinks, unreadable files and write failures are reported explicitly; the recovery transaction rolls back. The older raw-cache import API remains available for existing recovery tests, but is not wired into account startup.

Live fixtures exercise canonical metadata/body access, binary verified downloads with short reads and unsigned UIDs, failed identity/epoch/count/size checks, labels/colours/sort settings across restart, journal-backed flags/moves/APPEND, unchanged canonical IDs and bodies after moves, stale-snapshot fencing, rejection of direct local mutation bypasses, recovery idempotency/changed files/account isolation, loaded recovery-folder integration, unsafe files and injected rollback. They do not constitute a real-account network/UI smoke test, and whole-account folder removal/empty nonselectable folders are not yet reconciled.

Native fixtures exercise SELECT parsing, verified server identities, unique/ambiguous Message-ID recovery, folder settings, account/mailbox isolation, restart/idempotency, epoch resets, empty mailboxes and injected transaction failures, in addition to the older raw-message recovery tests. Canonical-schema fixtures also check local-only NULL fields, uniqueness, normal object serialization, clearing identities without losing content, and upgrading existing Mail3 folders/messages/bodies. Cutover persistence fixtures cover binary RFC822 body fidelity, stale download rejection, late-write rollback, atomic APPEND staging, frozen command identities, duplicate claims, destination UID collisions, account-local queue ordering, confirmed flag/delete/move/APPEND commits and uncertain-command recovery across restart. Scripted transport fixtures exercise actual SELECT/FETCH/STORE/COPY/APPEND commands, unsigned UID limits, empty flag lists, exact binary literals, partial reads/writes, failed literal uploads, missing/malformed/lost UIDPLUS replies, epoch/flag changes, persisted partial-move evidence and local commit failures after server success. Bridge tests execute uploads on another thread after closing the original store, including a body large enough to spill out of memory. On Linux, from the repository root with the normal debug dependencies built:

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