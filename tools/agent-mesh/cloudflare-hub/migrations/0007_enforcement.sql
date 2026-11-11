CREATE TABLE enrollment_settings (version INTEGER PRIMARY KEY, enforcement_mode TEXT NOT NULL CHECK(enforcement_mode IN ('record_only','enforce')), grace_until INTEGER NOT NULL, proof_interval_ms INTEGER NOT NULL, actor TEXT NOT NULL CHECK(actor='arin'), at INTEGER NOT NULL);
CREATE TABLE agent_connections (agent TEXT PRIMARY KEY, connected_at INTEGER NOT NULL);
CREATE TRIGGER settings_no_update BEFORE UPDATE ON enrollment_settings BEGIN SELECT RAISE(ABORT,'Settings history is append-only'); END;
CREATE TRIGGER settings_no_delete BEFORE DELETE ON enrollment_settings BEGIN SELECT RAISE(ABORT,'Settings history is append-only'); END;
