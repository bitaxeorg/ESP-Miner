import { Injectable } from '@angular/core';
import { MiningPool } from './mining-pool.interface';

@Injectable({
  providedIn: 'root',
})
export class CkPoolService implements MiningPool {
  private readonly explanations: Record<string, string> = {
    'Invalid nonce2 length': 'The nonce2 value does not match the expected length for this mining job.',
    'Worker mismatch': 'The worker name sent does not match the expected or authenticated worker.',
    'No nonce': 'The share submission was missing a nonce value.',
    'No ntime': 'The submission was missing the network time (ntime).',
    'No nonce2': 'The submission did not include the nonce2 value.',
    'No job_id': 'The submission did not specify a job ID.',
    'No username': 'The submission was missing a username (worker name).',
    'Invalid array size': 'The JSON array parameters had an unexpected size.',
    'Params not array': 'The parameters field was not formatted as a JSON array.',
    'Valid': 'This is not a rejection reason — it indicates a valid share.',
    'Invalid JobID': 'The job ID in the submission was not recognized or is no longer valid.',
    'Stale': 'While this share was being submitted, the network had already found a new block, making this share invalid.',
    'Ntime out of range': 'The provided ntime value was outside the allowed range.',
    'Duplicate': 'This share has already been submitted.',
    'Above target': 'The submitted share does not meet the minimum required difficulty.',
    'Invalid version mask': 'The version mask provided was invalid for this job.',
  };

  private readonly ckpoolRegex = /^(eusolo[46]?|solo[46]?)\.ckpool\.org/i;

  canHandle(url: string): boolean {
    return this.ckpoolRegex.test(url);
  }

  getRejectionExplanation(reason: string): string | null {
    return this.explanations[reason] ?? null;
  }

  getQuickLink(stratumURL: string, stratumUser: string): string | undefined {
    const match = stratumURL.match(this.ckpoolRegex);
    const address = stratumUser.split('.')[0];
    if (!match) return undefined;

    const region = match[1]; // e.g., 'eusolo4', 'solo6'
    return `https://${region}.ckpool.org/users/${encodeURIComponent(address)}`;
  }
}
