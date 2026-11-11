export class FeatureFailure extends Error {
  constructor(public status: number, text: string) { super(text); }
}
