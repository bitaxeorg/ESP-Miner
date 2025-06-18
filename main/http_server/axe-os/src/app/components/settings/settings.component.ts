import { HttpErrorResponse } from '@angular/common/http';
import { Component } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { Observable, shareReplay, startWith } from 'rxjs';
import { GithubUpdateService } from 'src/app/services/github-update.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { eASICModel } from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
  styleUrls: ['./settings.component.scss']
})
export class SettingsComponent {

  public form!: FormGroup;

  public showReleaseNotes = false;

  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  public info$: Observable<any>;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService,
    private githubUpdateService: GithubUpdateService
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}))


      this.info$.pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.ASICModel = info.ASICModel;
        this.form = this.fb.group({
          display: [info.display, [Validators.required]],
          flipscreen: [info.flipscreen == 1],
          invertscreen: [info.invertscreen == 1],
          displayTimeout: [info.displayTimeout, [Validators.required]],
          stratumURL: [info.stratumURL, [
            Validators.required,
            Validators.pattern(/^(?!.*stratum\+tcp:\/\/).*$/),
            Validators.pattern(/^[^:]*$/),
          ]],
          stratumPort: [info.stratumPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65535)
          ]],
          stratumUser: [info.stratumUser, [Validators.required]],
          stratumPassword: ['*****', [Validators.required]],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          autofanspeed: [info.autofanspeed == 1, [Validators.required]],
          temptarget: [info.temptarget, [Validators.required]],
          fanspeed: [info.fanspeed, [Validators.required]],
        });

        this.form.controls['autofanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autofanspeed'].value)
        ).subscribe(autofanspeed => {
          if (autofanspeed) {
            this.form.controls['fanspeed'].disable();
            this.form.controls['temptarget'].enable();
          } else {
            this.form.controls['fanspeed'].enable();
            this.form.controls['temptarget'].disable();
          }
        });
      });

  }
  public updateSystem() {

    const form = this.form.getRawValue();

    form.frequency = parseInt(form.frequency);
    form.coreVoltage = parseInt(form.coreVoltage);

    // bools to ints
    form.flipscreen = form.flipscreen == true ? 1 : 0;
    form.invertscreen = form.invertscreen == true ? 1 : 0;
    form.autofanspeed = form.autofanspeed == true ? 1 : 0;

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }

    this.systemService.updateSystem(undefined, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

  public restart() {
    this.systemService.restart().subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }

  // https://gist.github.com/elfefe/ef08e583e276e7617cd316ba2382fc40
  public simpleMarkdownParser(markdown: string): string {
    const toHTML = markdown
      .replace(/^#{1,6}\s+(.+)$/gim, '<h4 class="mt-2">$1</h4>')
      .replace(/\*\*(.+?)\*\*|__(.+?)__/gim, '<b>$1</b>')
      .replace(/\*(.+?)\*|_(.+?)_/gim, '<i>$1</i>')
      .replace(/\[(.*?)\]\((.*?)\s?(?:"(.*?)")?\)/gm, '<a href="$2" class="underline text-white" target="_blank">$1</a>')
      .replace(/^\s*[-+*]\s+(.+)$/gim, '<li>$1</li>')
      .replace(/\r\n\r\n/gim, '<br>');

    return toHTML.trim();
  }
}
