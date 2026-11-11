-- Authentication metadata only; provider tokens and raw session cookies are never stored.
CREATE TABLE portal_oauth_states (state_hash TEXT PRIMARY KEY, verifier_hash TEXT NOT NULL, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, consumed_at INTEGER);
CREATE TABLE portal_sessions (session_hash TEXT PRIMARY KEY, owner_id TEXT NOT NULL, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, revoked_at INTEGER);
CREATE TABLE portal_login_limits (bucket TEXT PRIMARY KEY, attempts INTEGER NOT NULL);
CREATE INDEX portal_sessions_expiry ON portal_sessions(expires_at);
