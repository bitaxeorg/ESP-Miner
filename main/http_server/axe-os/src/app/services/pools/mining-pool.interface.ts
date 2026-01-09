export interface MiningPool {
  canHandle(url: string): boolean;
  getRejectionExplanation(reason: string): string | null;
  getQuickLink(stratumURL: string, stratumUser: string): string | undefined;
}
