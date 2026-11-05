import { Injectable } from '@angular/core';

import { I18nService } from 'src/app/i18n/i18n.service';

@Injectable({
  providedIn: 'root'
})
export class ShareRejectionExplanationService {

  constructor(private i18n: I18nService) {}

  getLabel(key: string): string {
    const labelKey = `share_rejection.${key}.label`;
    const translated = this.i18n.t(labelKey);
    return translated === labelKey ? key : translated;
  }

  getExplanation(key: string): string | null {
    const explanationKey = `share_rejection.${key}.explanation`;
    const translated = this.i18n.t(explanationKey);
    return translated === explanationKey ? null : translated;
  }
}
