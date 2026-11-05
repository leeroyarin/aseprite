-- Additive rule book and usage/handoff evidence. No seed data or enrollment enforcement.
CREATE TABLE rulebook_revisions (version INTEGER PRIMARY KEY CHECK(version>0), sha256 TEXT NOT NULL CHECK(length(sha256)=64), body TEXT NOT NULL, author TEXT NOT NULL CHECK(author='arin'), created_at INTEGER NOT NULL, note TEXT NOT NULL);
CREATE TABLE usage_reports (seq INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL UNIQUE, project_id TEXT NOT NULL REFERENCES projects(id), agent TEXT NOT NULL, received_at INTEGER NOT NULL, metrics_json TEXT NOT NULL, context_remaining_percent REAL, budget_remaining_percent REAL, handoff_recommended INTEGER NOT NULL CHECK(handoff_recommended IN (0,1)), source TEXT NOT NULL, unavailable_reason TEXT, request_hash TEXT NOT NULL);
CREATE INDEX usage_latest ON usage_reports(project_id,agent,seq DESC);
CREATE TABLE task_handoffs (seq INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL UNIQUE, task_id TEXT NOT NULL REFERENCES tasks(id), project_id TEXT NOT NULL REFERENCES projects(id), author TEXT NOT NULL, received_at INTEGER NOT NULL, work_state TEXT NOT NULL CHECK(work_state IN ('incomplete','complete','blocked')), push_state TEXT NOT NULL CHECK(push_state IN ('pushed','not_pushed')), repository TEXT NOT NULL, branch TEXT NOT NULL, commit_hash TEXT, body_json TEXT NOT NULL, request_hash TEXT NOT NULL);
CREATE INDEX handoffs_latest ON task_handoffs(project_id,task_id,seq DESC);
CREATE TABLE handoff_acceptances (seq INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL UNIQUE, handoff_id TEXT NOT NULL REFERENCES task_handoffs(id), agent TEXT NOT NULL, received_at INTEGER NOT NULL, checked_out_commit TEXT NOT NULL, notes TEXT NOT NULL, UNIQUE(handoff_id,agent));
CREATE TRIGGER rulebook_no_update BEFORE UPDATE ON rulebook_revisions BEGIN SELECT RAISE(ABORT,'Rule book history is append-only'); END;
CREATE TRIGGER rulebook_no_delete BEFORE DELETE ON rulebook_revisions BEGIN SELECT RAISE(ABORT,'Rule book history is append-only'); END;
CREATE TRIGGER usage_no_update BEFORE UPDATE ON usage_reports BEGIN SELECT RAISE(ABORT,'Usage history is append-only'); END;
CREATE TRIGGER usage_no_delete BEFORE DELETE ON usage_reports BEGIN SELECT RAISE(ABORT,'Usage history is append-only'); END;
CREATE TRIGGER handoffs_no_update BEFORE UPDATE ON task_handoffs BEGIN SELECT RAISE(ABORT,'Handoff history is append-only'); END;
CREATE TRIGGER handoffs_no_delete BEFORE DELETE ON task_handoffs BEGIN SELECT RAISE(ABORT,'Handoff history is append-only'); END;
CREATE TRIGGER acceptances_no_update BEFORE UPDATE ON handoff_acceptances BEGIN SELECT RAISE(ABORT,'Handoff acceptance history is append-only'); END;
CREATE TRIGGER acceptances_no_delete BEFORE DELETE ON handoff_acceptances BEGIN SELECT RAISE(ABORT,'Handoff acceptance history is append-only'); END;
