import { Injectable } from '@angular/core';
import shareRejectionExplanations from '../../assets/share-rejection-explanations.json';
import { I18nService } from 'src/app/i18n/i18n.service';

@Injectable({
  providedIn: 'root'
})
export class ShareRejectionExplanationService {

  private readonly reasonMap: Map<string, string>;

  constructor(private i18n: I18nService) {
    const merged = Object.assign({}, ...shareRejectionExplanations);
    this.reasonMap = new Map(Object.entries(merged));
  }

  getLabel(key: string): string {
    const translationKey = this.reasonMap.get(key);
    if (!translationKey) {
      return key;
    }
    const labelKey = translationKey.replace('miner.rejection_explanations.', 'miner.rejection_reasons.');
    const translated = this.i18n.t(labelKey);
    return translated === labelKey ? key : translated;
  }

  getExplanation(key: string): string | null {
    const translationKey = this.reasonMap.get(key);
    if (!translationKey) {
      return null;
    }
    const translated = this.i18n.t(translationKey);
    return translated === translationKey ? null : translated;
  }
}
