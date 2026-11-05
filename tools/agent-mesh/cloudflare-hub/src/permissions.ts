/** Foundation policies for Part 2 feature routes. Default-deny; no message instructions. */
export interface HubIdentity { role: "admin" | "agent"; agent: string; actor: string }
export interface ChannelAccess { type: "announce" | "group" | "dm"; members: readonly string[] }
export function canReadChannel(identity: HubIdentity, channel: ChannelAccess): boolean {
  return identity.role==="admin" || channel.members.includes(identity.agent);
}
export function canPostChannel(identity: HubIdentity, channel: ChannelAccess): boolean {
  return canReadChannel(identity,channel) && (channel.type!=="announce" || identity.role==="admin");
}
export function canSuggestDocument(identity: HubIdentity, projectAgents: readonly string[]): boolean {
  return identity.role==="agent" && projectAgents.includes(identity.agent);
}
export function canApproveDocument(identity: HubIdentity): boolean { return identity.role==="admin"; }
export function canEditDocument(identity: HubIdentity): boolean { return identity.role==="admin"; }
