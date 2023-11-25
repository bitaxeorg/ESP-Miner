import { Component } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { catchError, map, Observable, of, startWith, switchMap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-swarm',
  templateUrl: './swarm.component.html',
  styleUrls: ['./swarm.component.scss']
})
export class SwarmComponent {

  public form: FormGroup;

  public swarm$: Observable<Observable<any>[]>;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService
  ) {
    this.form = this.fb.group({
      ip: []
    });

    this.swarm$ = this.systemService.getSwarmInfo().pipe(
      map(swarmInfo => {
        return swarmInfo.map(({ ip }) => {
          // Make individual API calls for each IP
          return this.systemService.getInfo(`http://${ip}`).pipe(
            startWith({ ip }),
            map(info => {
              return {
                ip,
                ...info
              };
            }),
            catchError(error => {
              return of(null);
            })
          );
        });
      })
    );


  }


  public add() {
    const ip = this.form.value.ip;

    this.systemService.getSwarmInfo().pipe(
      switchMap((swarm: any) => {
        return this.systemService.updateSwarm([{ ip }, ...swarm])
      })
    ).subscribe({
      next: () => {
        this.toastr.success('Success!', 'Saved.');
      },
      error: (err) => {
        this.toastr.error('Error.', `Could not save. ${err.message}`);
      },
      complete: () => {
        this.form.reset();
      }
    });

  }

}
