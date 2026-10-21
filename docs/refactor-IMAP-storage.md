# IMAP Mail Store backend storage

## Existing implementation

Currently the IMAP Store3 implementation that lives in 'scribe/trunk_os/src/Store3Imap' uses the filesystem to store it's local cache of folder indexes (as .xml) and email (as .eml files). This is probably NOT ideal and dupelicates all the functionality in the mail3 storage implementation in 'scribe/trunk_os/src/Store3Mail3' which uses an Sqlite3 database as it's permanent persistance.

It makes sense to me to move the IMAP implementation to using the SAME Sqlite3 database as the Mail3 store. There are several benefits:
    - moving email between a Mail3 folder and an IMAP folder just reparents the Mail record to a new folder rather than making an expensive copy between 2 separate backends. Obviously the IMAP server will need to get involved to propagate that change to there.
    - I would like to be able to speed up searching using the Sqlite full text search functionality such that it works for IMAP email too. So bringing all the IMAP email into the Sqlite database could enable that.

However there are a lot of risks too. Things like threading and ownership of data causing difficult to track down bugs. Added complexity to APIs making maintainence work harder. Or new performance issues.

## Plan for merging the backends to use the same Sqlite database

### Implementation progress

The first implementation slice adds IMAP mailbox, message-mapping and operation-journal tables to the Mail3 schema upgrade path, with uniqueness indexes for mailbox identity and UID scope. `LMail3Store` now exposes typed mailbox and message-mapping load/save/delete methods, guarded by its existing owning-thread rule. Persistent IMAP account connections now receive a non-owning reference to the already-loaded default Mail3 backend; one-off IMAP connections remain unchanged. The IMAP runtime still reads and writes its existing filesystem cache; using the injected backend, operation-journal access and legacy-cache migration have not yet been implemented.

Use the existing `LMail3Store` database as the single local source of truth for both Mail3 and IMAP mail. Keep the IMAP protocol engine and its `LDataStoreI`-facing behavior, but make it a protocol adapter over the shared store rather than a second owner of cached messages and folders.

Reuse the existing `Folder`, `Mail`, and `MailSegs` records for the local folder tree, message properties, and MIME content. A message has one local mail ID and one parent folder; moving it between local folders updates its parent reference instead of copying its MIME tree. Keep IMAP-only state in extension tables rather than overloading Mail3 fields:

- `ImapMailbox`: one row per IMAP-backed folder, keyed to its local folder and account; stores the remote mailbox name, delimiter, UIDVALIDITY, UIDNEXT and other synchronization cursors as needed.
- `ImapMessage`: one row per IMAP-backed message, keyed to its local mail ID; stores UID, remote flags, mod-sequence and pending local flag changes. Enforce UID uniqueness within the account/mailbox/UIDVALIDITY scope.
- `ImapOperation`: a durable journal for operations that must be applied to the server, including cross-folder moves, appends, flag updates and deletes. Store enough state to retry and reconcile after a restart.

The exact extension columns should follow the IMAP commands and state already used by `ImapThread`; do not treat a UID as globally unique or valid across UIDVALIDITY changes. Keep account identity explicit even if the current application account ID is used as its key.

### Ownership and data flow

`LMail3Store` owns the SQLite connection, schema upgrades, transactions, canonical folder/mail objects and MIME BLOB streams. The IMAP adapter receives a reference to that store and maps protocol events to canonical mail records plus the IMAP extension rows. It must not open a second SQLite connection to the same database.

The IMAP network worker continues to perform network I/O and post results to the store-owning thread. Database and canonical object mutations happen on the owning thread, consistent with `LMail3Store`'s existing thread affinity. Use existing store transactions for each local state transition. UI notifications are emitted only after the corresponding transaction commits.

Keep the existing IMAP object/API boundary initially. `ImapMail` and `ImapFolder` can act as adapters or views over canonical objects while callers are migrated; avoid making the network thread own shared `LMail3Mail` instances. In particular, preserve delayed-operation status, folder loading behavior, and event ordering during the transition.

### IMAP synchronization and moves

An SQLite transaction cannot include an IMAP server command. Do not implement cross-boundary moves as a bare local `ParentId` update. Record the requested operation durably, apply it to the server, then reconcile the resulting UID/flags and local parent in a transaction. If the UI needs optimistic moves, represent that pending state explicitly and retain enough source mapping to recover or roll back after failure. Retries must be idempotent, and reconciliation must account for servers without the IMAP MOVE extension (for example, COPY followed by delete/expunge).

For a move entirely within the local Mail3 store, update the existing mail row's parent inside a transaction; its MIME BLOBs remain unchanged. For a move involving an IMAP mailbox, keep the `Mail.Id` and MIME content where possible, but update or replace the IMAP mapping only when the server confirms the destination identity. Moving from a Mail3-only folder to IMAP requires an APPEND and creation of a new server UID mapping; moving from IMAP to a Mail3-only folder requires the configured server-side delete policy to be completed or recorded as pending.

Retain the original RFC822 bytes for IMAP APPEND/retry where re-encoding the MIME tree could change message content. Before implementation, determine whether the current Mail3 segment representation can guarantee byte-for-byte preservation; otherwise add a narrowly scoped raw-message BLOB keyed by mail ID and define when it can be discarded.

### Search

Add an SQLite FTS index for searchable text from both Mail3 and IMAP mail, indexed by canonical mail ID. Index headers/body text only, never attachment binary data. Keep the FTS row synchronized with message creation, update, move and deletion in the same transaction where supported; moving a message should not require reindexing its content. Check that the SQLite build used by all supported targets includes the selected FTS module, and provide a rebuild path for the derived index.

### Migration and rollout

1. Define schema versioning and add the IMAP extension tables through the existing Mail3 database upgrade path. Migration must be restartable and must not discard legacy cache files.
2. Import each account's existing folder metadata and cached `.eml` files into canonical folders, mail rows and MIME storage. Preserve server UIDs, UIDVALIDITY-related state, flags, folder display settings and any not-yet-downloaded message metadata. Make the importer idempotent and report corrupt or missing cache data explicitly.
3. Verify imported folder/message counts, UID mappings, representative MIME content and attachment streams against the old cache. Keep the old cache as a recoverable source until verification succeeds.
4. Switch IMAP reads and writes to the shared database, with one authoritative write path. Avoid prolonged dual writes; they create two conflicting sources of truth.
5. Add and validate FTS indexing/rebuild behavior independently from the storage cutover.
6. Remove legacy XML/Protobuf folder indexes and `.eml` cache writes only after upgrade, restart, retry and rollback behavior has been exercised. Retain a deliberate recovery/backup policy for users upgrading from older versions.

### Invariants and acceptance checks

- Every locally cached message has one canonical mail row and one parent folder; its MIME segments are not duplicated by a local move.
- Every IMAP mapping references an existing mail row and the correct account/mailbox/UIDVALIDITY scope.
- A message that is not fully downloaded can retain its IMAP metadata without pretending that its RFC822 content is available locally.
- Local database commits and UI notifications do not claim server success before the server operation has been confirmed.
- Folder synchronization, reconnect, expunge, UIDVALIDITY reset and operation retry preserve or explicitly reconcile local/server state.
- Existing Mail3-only storage, folders, MIME streaming, backup/restore and schema upgrades continue to work.
- FTS results identify canonical mail IDs and remain correct after updates, deletes and moves.

### Open design checks before implementation

- Confirm whether one `LMail3Store` instance/database is available to every IMAP account that should share the Mail3 store, and define how that instance is injected and kept alive.
- Confirm SQLite thread/connection constraints and transaction ownership around IMAP callbacks.
- Inventory the complete IMAP sync state currently stored in folder metadata and per-message XML/protobuf before finalizing the extension schema.
- Decide the user-visible failure policy for server-backed moves and deletes, including offline operation and retries.
- Verify raw RFC822 fidelity and FTS availability across Linux, macOS, Windows and other supported builds.