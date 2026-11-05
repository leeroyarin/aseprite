# Agent Hub cloud implementation plan

Authority: docs/USER-REQUIREMENTS.md, supplied by Arin on 2026-11-05. This supersedes the
local FastAPI/tunnel architecture. The address and Worker name remain agent-mesh-hub.
Agents connect out over HTTPS/WebSocket; there is no agent-hosted backend or tunnel.

## Gates

- Part 1: foundation (this branch). D1 schema, header auth, Access identity hub/actor arin,
  transport paging/idempotence/limits, permissions, CORS, append-only history. Stop for check.
- Part 2, source implemented and awaiting review: projects/channels; registry; five-minute Cron supervision and
  task failover; versioned plan/onboarding and suggestions/acks; parent-authenticated sub labels;
  activity/search; portal live stream; portal assets on this same Worker, in that order.
- Part 3, source implemented and awaiting review: owner-edited versioned rule book with SHA-256 text hash and separate command/chat records.
- Part 4: explicit read/accept/install/prove enrollment and outbound-only hub_command_log.py;
  discover actual device chat paths, never guess them. Log commands only, append-only, redact,
  deduplicate by message ID; never execute commands or modify original chats.
- Part 5: record_only default; no access loss for unenrolled/failed challenge agents. Owner-only
  enforcement_mode and grace period. Test enforce in isolation only, including revoke closing WS.
- Part 6: onboarding explains actual rule version/hash, device path discovery, enrollment,
  acknowledgements, record-only banner, and the existing Connect your agent section.

Stop after every part and wait for Arin's check. No merge, publish, production secrets/token
changes, or cloud resource provisioning without approval. Existing legacy paths stay usable.
Local prototype retirement is explicitly gated on Worker feature/test parity; it is not a
runtime dependency of this branch and no local-server code is imported into the Worker.

## Reuse

migrations/0002_foundation.sql ports the local SQLite projects/channels/messages/docs/revisions/
activity/search model. src/redaction.ts ports server/redaction.py. Test policy cases adapt the
local API permission expectations and run transport against actual workerd, local D1 and DO.
The local API.md is a contract reference for Part 2; its localhost URLs are not deployment targets.
Reuse only outbound client portions of the old sidecar; its local HTTP server is incompatible
with this architecture and will not be used by the cloud hub.

## Honest enrollment limit (required for Parts 3-6)

Enrollment shows an agent ran the script and answered correctly. It cannot prove the agent
will always follow the rules. The hub's permissions, limits and rate limits are the real safety layer.
No enrollment or enforcement feature is implemented or enabled in Part 1.


## Part 2 checkpoint

See PART2-REVIEW.md and API.md. Feature source and isolated tests are complete; no production
resources or configuration changed. Stop here for owner check before Part 3. The same-Worker
portal replaces no current production address. Local runtime retirement waits for approved cloud
cutover and actual agent/portal parity, which local source tests cannot establish.


## Approved extension and Part 3 checkpoint

Arin confirmed Part 2 coverage/permissions and requested real usage reporting, timely pushed
incomplete work branches and final handoff evidence. Part 3 implements the rule book plus those
explicit additions; see PART3-REVIEW.md, API.md and docs/CONTINUATION.md. No hub message executes
work, and usage/push evidence is honestly labelled agent-reported. Stop for the Part 3 owner check
before enrollment. Production remains separately gated.
