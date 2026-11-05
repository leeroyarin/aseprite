-- Additive Step 1 transport ledger. No seed data and no legacy-table changes.
CREATE TABLE mesh_messages (
  seq INTEGER PRIMARY KEY AUTOINCREMENT,
  id TEXT NOT NULL UNIQUE,
  project TEXT NOT NULL,
  from_agent TEXT NOT NULL,
  to_agent TEXT NOT NULL,
  type TEXT NOT NULL,
  reply_to TEXT,
  body TEXT NOT NULL,
  timestamp TEXT NOT NULL,
  subject TEXT NOT NULL,
  request_hash TEXT NOT NULL,
  source TEXT NOT NULL CHECK(source IN ('legacy','v2'))
);
CREATE INDEX mesh_messages_project_seq ON mesh_messages(project,seq);
CREATE INDEX mesh_messages_recipient_seq ON mesh_messages(to_agent,seq);
CREATE TRIGGER mesh_messages_no_update BEFORE UPDATE ON mesh_messages
BEGIN SELECT RAISE(ABORT,'Message history is append-only'); END;
CREATE TRIGGER mesh_messages_no_delete BEFORE DELETE ON mesh_messages
BEGIN SELECT RAISE(ABORT,'Message history is append-only'); END;
