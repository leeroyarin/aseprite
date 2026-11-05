-- Enrollment records only. No access restriction, token change or seed data.
CREATE TABLE enrollment_audit (seq INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL UNIQUE, agent TEXT NOT NULL, actor TEXT NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('accept','challenge','proof','revoke','allow')), at INTEGER NOT NULL, rulebook_version INTEGER REFERENCES rulebook_revisions(version), rulebook_sha256 TEXT, payload_json TEXT NOT NULL, CHECK(kind NOT IN ('revoke','allow') OR actor='arin'));
CREATE INDEX enrollment_agent_audit ON enrollment_audit(agent,seq DESC);
CREATE TABLE enrollment_acceptances (id TEXT PRIMARY KEY, agent TEXT NOT NULL, rulebook_version INTEGER NOT NULL REFERENCES rulebook_revisions(version), rulebook_sha256 TEXT NOT NULL, accepted_at TEXT NOT NULL, event_id TEXT NOT NULL UNIQUE REFERENCES enrollment_audit(id), request_hash TEXT NOT NULL);
CREATE TABLE enrollment_challenges (id TEXT PRIMARY KEY, agent TEXT NOT NULL, acceptance_id TEXT NOT NULL REFERENCES enrollment_acceptances(id), project_id TEXT NOT NULL REFERENCES projects(id), code_hash TEXT NOT NULL, script_sha256 TEXT NOT NULL, chat_path TEXT NOT NULL, commands_path TEXT NOT NULL, discovery_json TEXT NOT NULL, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, message_id TEXT NOT NULL UNIQUE, event_id TEXT NOT NULL UNIQUE REFERENCES enrollment_audit(id), request_hash TEXT NOT NULL);
CREATE TABLE enrollment_proofs (id TEXT PRIMARY KEY, challenge_id TEXT NOT NULL REFERENCES enrollment_challenges(id), agent TEXT NOT NULL, success INTEGER NOT NULL CHECK(success IN (0,1)), reason TEXT NOT NULL, evidence_json TEXT NOT NULL, event_id TEXT NOT NULL UNIQUE REFERENCES enrollment_audit(id), request_hash TEXT NOT NULL);
CREATE TABLE enrollment_consumed (challenge_id TEXT PRIMARY KEY REFERENCES enrollment_challenges(id), proof_id TEXT NOT NULL UNIQUE REFERENCES enrollment_proofs(id));
CREATE TRIGGER enrollment_audit_no_update BEFORE UPDATE ON enrollment_audit BEGIN SELECT RAISE(ABORT,'Enrollment audit is append-only'); END;
CREATE TRIGGER enrollment_audit_no_delete BEFORE DELETE ON enrollment_audit BEGIN SELECT RAISE(ABORT,'Enrollment audit is append-only'); END;
CREATE TRIGGER enrollment_acceptances_no_update BEFORE UPDATE ON enrollment_acceptances BEGIN SELECT RAISE(ABORT,'Enrollment acceptance is append-only'); END;
CREATE TRIGGER enrollment_acceptances_no_delete BEFORE DELETE ON enrollment_acceptances BEGIN SELECT RAISE(ABORT,'Enrollment acceptance is append-only'); END;
CREATE TRIGGER challenges_no_update BEFORE UPDATE ON enrollment_challenges BEGIN SELECT RAISE(ABORT,'Enrollment challenge is append-only'); END;
CREATE TRIGGER challenges_no_delete BEFORE DELETE ON enrollment_challenges BEGIN SELECT RAISE(ABORT,'Enrollment challenge is append-only'); END;
CREATE TRIGGER proofs_no_update BEFORE UPDATE ON enrollment_proofs BEGIN SELECT RAISE(ABORT,'Enrollment proof is append-only'); END;
CREATE TRIGGER proofs_no_delete BEFORE DELETE ON enrollment_proofs BEGIN SELECT RAISE(ABORT,'Enrollment proof is append-only'); END;
CREATE TRIGGER consumed_no_update BEFORE UPDATE ON enrollment_consumed BEGIN SELECT RAISE(ABORT,'Challenge results are append-only'); END;
CREATE TRIGGER consumed_no_delete BEFORE DELETE ON enrollment_consumed BEGIN SELECT RAISE(ABORT,'Challenge results are append-only'); END;
