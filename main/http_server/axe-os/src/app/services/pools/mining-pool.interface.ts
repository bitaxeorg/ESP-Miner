export interface MiningPool {
    canHandle(stratumURL: string): boolean;
    getRejectionExplanation(reason: string): string | null;
    getQuickLink(stratumURL: string, stratumUser: string): string | undefined;
  }