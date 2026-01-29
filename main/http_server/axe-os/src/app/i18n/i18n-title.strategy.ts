import { Injectable } from '@angular/core';
import { Title } from '@angular/platform-browser';
import { RouterStateSnapshot, TitleStrategy } from '@angular/router';

import { I18nService } from './i18n.service';

@Injectable()
export class I18nTitleStrategy extends TitleStrategy {
  constructor(private readonly title: Title, private readonly i18n: I18nService) {
    super();
  }

  override updateTitle(snapshot: RouterStateSnapshot): void {
    const titleKey = this.buildTitle(snapshot);
    if (!titleKey) {
      return;
    }

    const appName = this.i18n.t('common.app_name');
    const translated = titleKey === 'common.app_name' ? appName : this.i18n.t(titleKey);
    const title = titleKey === 'common.app_name' ? appName : `${appName} ${translated}`;

    this.title.setTitle(title.trim());
  }
}
