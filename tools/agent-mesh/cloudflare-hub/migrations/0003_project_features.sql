-- Additive feature projections and immutable event history. No seed data.
ALTER TABLE mesh_messages ADD COLUMN sub TEXT;
ALTER TABLE messages ADD COLUMN sub TEXT;
CREATE TABLE agent_registry (id TEXT PRIMARY KEY, name TEXT NOT NULL, role TEXT NOT NULL DEFAULT '', last_seen INTEGER NOT NULL DEFAULT 0, health TEXT NOT NULL DEFAULT 'Late', status_json TEXT NOT NULL DEFAULT '{}', checked_at INTEGER NOT NULL DEFAULT 0, registered_at INTEGER NOT NULL DEFAULT 0);
CREATE TABLE status_reports (seq INTEGER PRIMARY KEY AUTOINCREMENT, agent TEXT NOT NULL, ts INTEGER NOT NULL, body TEXT NOT NULL);
CREATE TABLE tasks (id TEXT PRIMARY KEY, project_id TEXT NOT NULL REFERENCES projects(id), title TEXT NOT NULL, owner TEXT NOT NULL, assignee TEXT NOT NULL, state TEXT NOT NULL DEFAULT 'open' CHECK(state IN ('open','done')), created_at INTEGER NOT NULL);
CREATE TABLE task_events (seq INTEGER PRIMARY KEY AUTOINCREMENT, task_id TEXT NOT NULL REFERENCES tasks(id), ts INTEGER NOT NULL, actor TEXT NOT NULL, event TEXT NOT NULL, assignee TEXT NOT NULL);
CREATE TABLE doc_acks (project_id TEXT NOT NULL, doc_id TEXT NOT NULL, version INTEGER NOT NULL, agent_id TEXT NOT NULL, acked_at INTEGER NOT NULL, PRIMARY KEY(project_id,doc_id,version,agent_id), FOREIGN KEY(project_id,doc_id,version) REFERENCES doc_revisions(project_id,doc_id,rev));
CREATE TABLE suggestion_decisions (id TEXT PRIMARY KEY, suggestion_id TEXT NOT NULL REFERENCES doc_suggestions(id), actor TEXT NOT NULL, decision TEXT NOT NULL, ts INTEGER NOT NULL);
CREATE TABLE feature_outbox (id TEXT PRIMARY KEY, envelope TEXT NOT NULL, delivered INTEGER NOT NULL DEFAULT 0);
CREATE INDEX feature_outbox_pending ON feature_outbox(delivered);
CREATE INDEX activity_project_seq ON activity_log(project_id,seq);
CREATE INDEX task_project_owner ON tasks(project_id,owner,state);
CREATE TRIGGER reports_no_update BEFORE UPDATE ON status_reports BEGIN SELECT RAISE(ABORT,'Reports are append-only'); END;
CREATE TRIGGER reports_no_delete BEFORE DELETE ON status_reports BEGIN SELECT RAISE(ABORT,'Reports are append-only'); END;
CREATE TRIGGER task_events_no_update BEFORE UPDATE ON task_events BEGIN SELECT RAISE(ABORT,'Task history is append-only'); END;
CREATE TRIGGER task_events_no_delete BEFORE DELETE ON task_events BEGIN SELECT RAISE(ABORT,'Task history is append-only'); END;
CREATE TRIGGER acks_no_update BEFORE UPDATE ON doc_acks BEGIN SELECT RAISE(ABORT,'Acknowledgements are append-only'); END;
CREATE TRIGGER acks_no_delete BEFORE DELETE ON doc_acks BEGIN SELECT RAISE(ABORT,'Acknowledgements are append-only'); END;
CREATE TRIGGER decisions_no_update BEFORE UPDATE ON suggestion_decisions BEGIN SELECT RAISE(ABORT,'Decisions are append-only'); END;
CREATE TRIGGER decisions_no_delete BEFORE DELETE ON suggestion_decisions BEGIN SELECT RAISE(ABORT,'Decisions are append-only'); END;

-- Index native transport traffic too. Channel outbox envelopes already have a
-- canonical channel activity row; avoid indexing one copy per recipient.
CREATE TRIGGER mesh_activity AFTER INSERT ON mesh_messages WHEN NOT EXISTS (SELECT 1 FROM feature_outbox WHERE id=NEW.id) BEGIN
  INSERT INTO activity_log(id,project_id,ts,time,agent,kind,event_type,title,body,metadata_json)
  VALUES('mesh:'||NEW.id,NEW.project,COALESCE(CAST(strftime('%s',NEW.timestamp) AS INTEGER)*1000,0),NEW.timestamp,NEW.from_agent,'message','mesh.message',NEW.subject,NEW.body,json_object('to',NEW.to_agent));
  INSERT INTO search_fts(project_id,entry_id,kind,agent,ts,title,body)
  VALUES(NEW.project,'mesh:'||NEW.id,'activity',NEW.from_agent,COALESCE(CAST(strftime('%s',NEW.timestamp) AS INTEGER)*1000,0),NEW.subject,NEW.body);
END;
