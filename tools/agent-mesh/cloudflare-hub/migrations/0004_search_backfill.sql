-- Derived index backfill only; never update or delete the transport ledger.
INSERT OR IGNORE INTO activity_log(id,project_id,ts,time,agent,kind,event_type,title,body,metadata_json)
SELECT 'mesh:'||id,project,COALESCE(CAST(strftime('%s',timestamp) AS INTEGER)*1000,0),timestamp,from_agent,'message','mesh.message',subject,body,json_object('to',to_agent)
FROM mesh_messages WHERE NOT EXISTS (SELECT 1 FROM feature_outbox o WHERE o.id=mesh_messages.id);
INSERT INTO search_fts(project_id,entry_id,kind,agent,ts,title,body)
SELECT project_id,id,'activity',agent,ts,title,body FROM activity_log a
WHERE NOT EXISTS (SELECT 1 FROM search_fts s WHERE s.entry_id=a.id);
