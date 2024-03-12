import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { startWith } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { eASICModel } from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-edit',
  templateUrl: './edit.component.html',
  styleUrls: ['./edit.component.scss']
})
export class EditComponent implements OnInit {

  public form!: FormGroup;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;


  public devToolsOpen: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  @Input() uri = '';

  public BM1397DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '425 (default)', value: 425 },
    { name: '450', value: 450 },
    { name: '475', value: 475 },
    { name: '485', value: 485 },
    { name: '500', value: 500 },
    { name: '525', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
  ];

  public BM1366DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '425', value: 425 },
    { name: '450', value: 450 },
    { name: '475', value: 475 },
    { name: '485 (default)', value: 485 },
    { name: '500', value: 500 },
    { name: '525', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
  ];

  public BM1368DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '425', value: 425 },
    { name: '450', value: 450 },
    { name: '475', value: 475 },
    { name: '490 (default)', value: 490 },
    { name: '500', value: 500 },
    { name: '525', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
  ];

  public BM1397CoreVoltage = [
    { name: '1100', value: 1100 },
    { name: '1150', value: 1150 },
    { name: '1200', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1300', value: 1300 },
    { name: '1350', value: 1350 },
    { name: '1400', value: 1400 },
    { name: '1450', value: 1450 },
    { name: '1500', value: 1500 },
  ];
  public BM1366CoreVoltage = [
    { name: '1100', value: 1100 },
    { name: '1150', value: 1150 },
    { name: '1200 (default)', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1300', value: 1300 },
  ];
  public BM1368CoreVoltage = [
    { name: '1100', value: 1100 },
    { name: '1150', value: 1150 },
    { name: '1200', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1300', value: 1300 },
  ];

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

    window.addEventListener('resize', this.checkDevTools);
    this.checkDevTools();

  }
  ngOnInit(): void {
    this.systemService.getInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.ASICModel = info.ASICModel;
        this.form = this.fb.group({
          flipscreen: [info.flipscreen == 1],
          invertscreen: [info.invertscreen == 1],
          stratumURL: [info.stratumURL, [
            Validators.required,
            Validators.pattern(/^(?!.*stratum\+tcp:\/\/).*$/),
            Validators.pattern(/^[^:]*$/),
          ]],
          stratumPort: [info.stratumPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65353)
          ]],
          stratumUser: [info.stratumUser, [Validators.required]],
          stratumPassword: ['password', [Validators.required]],
          ssid: [info.ssid, [Validators.required]],
          wifiPass: ['password'],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          autofanspeed: [info.autofanspeed == 1, [Validators.required]],
          invertfanpolarity: [info.invertfanpolarity == 1, [Validators.required]],
          fanspeed: [info.fanspeed, [Validators.required]],
        });

        this.form.controls['autofanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autofanspeed'].value)
        ).subscribe(autofanspeed => {
          if (autofanspeed) {
            this.form.controls['fanspeed'].disable();
          } else {
            this.form.controls['fanspeed'].enable();
          }
        });
      });
  }


  private checkDevTools = () => {
    if (
      window.outerWidth - window.innerWidth > 160 ||
      window.outerHeight - window.innerHeight > 160
    ) {
      this.devToolsOpen = true;
    } else {
      this.devToolsOpen = false;
    }
  };

  public updateSystem() {

    const form = this.form.getRawValue();

    if (form.wifiPass === 'password') {
      delete form.wifiPass;
    }
    if (form.stratumPassword === 'password') {
      delete form.stratumPassword;
    }

    this.systemService.updateSystem(this.uri, form)
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

}
