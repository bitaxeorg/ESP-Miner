import { Injectable } from '@angular/core';
import shareRejectionExplanations from '../../assets/share-rejection-explanations.json';

@Injectable({
  providedIn: 'root'
})
export class ShareRejectionExplanationService {

  private readonly reasonMap: Map<string, string>;

  constructor() {
    const merged = Object.assign({}, ...shareRejectionExplanations);
    this.reasonMap = new Map(Object.entries(merged));
  }

  getExplanation(key: string): string | null {
    return this.reasonMap.get(key) ?? null;
  }
}
