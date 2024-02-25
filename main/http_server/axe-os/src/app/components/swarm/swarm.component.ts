import { Component } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { BehaviorSubject, catchError, combineLatest, forkJoin, map, Observable, of, startWith, switchMap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-swarm',
  templateUrl: './swarm.component.html',
  styleUrls: ['./swarm.component.scss']
})
export class SwarmComponent {

  public form: FormGroup;

  public swarm$: Observable<Observable<any>[]>;

  public refresh$: BehaviorSubject<null> = new BehaviorSubject(null);

  public selectedAxeOs: any = null;
  public showEdit = false;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService
  ) {
    this.form = this.fb.group({
      ip: [null, [Validators.required, Validators.pattern('(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)')]]
    });

    this.swarm$ = this.systemService.getSwarmInfo().pipe(
      map(swarmInfo => {
        return swarmInfo.map(({ ip }) => {
          // Make individual API calls for each IP
          return this.refresh$.pipe(
            switchMap(() => {
              return this.systemService.getInfo(`http://${ip}`);
            })
          ).pipe(
            startWith({ ip }),
            map(info => {
              return {
                ip,
                ...info
              };
            }),
            catchError(error => {
              return of({ ip, error: true });
            })
          );
        });
      })
    );


  }


  public add() {
    const newIp = this.form.value.ip;

    combineLatest([this.systemService.getSwarmInfo('http://' + newIp), this.systemService.getSwarmInfo()]).pipe(
      switchMap(([newSwarmInfo, existingSwarmInfo]) => {

        if (existingSwarmInfo.length < 1) {
          existingSwarmInfo.push({ ip: window.location.host });
        }


        const swarmUpdate = existingSwarmInfo.map(({ ip }) => {
          return this.systemService.updateSwarm('http://' + ip, [{ ip: newIp }, ...newSwarmInfo, ...existingSwarmInfo])
        });

        const newAxeOs = this.systemService.updateSwarm('http://' + newIp, [{ ip: newIp }, ...existingSwarmInfo])

        return forkJoin([newAxeOs, ...swarmUpdate]);

      })
    ).subscribe({
      next: () => {
        this.toastr.success('Success!', 'Saved.');
        window.location.reload();
      },
      error: (err) => {
        this.toastr.error('Error.', `Could not save. ${err.message}`);
      },
      complete: () => {
        this.form.reset();
      }
    });

  }

  public refresh() {
    this.refresh$.next(null);
  }

  public edit(axe: any) {
    this.selectedAxeOs = axe;
    this.showEdit = true;
  }

  public restart(axe: any) {
    this.systemService.restart(`http://${axe.ip}`).subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }

  public remove(axeOs: any) {
    this.systemService.getSwarmInfo().pipe(
      switchMap((swarmInfo) => {

        const newSwarm = swarmInfo.filter((s: any) => s.ip != axeOs.ip);

        const swarmUpdate = newSwarm.map(({ ip }) => {
          return this.systemService.updateSwarm('http://' + ip, newSwarm)
        });

        const removedAxeOs = this.systemService.updateSwarm('http://' + axeOs.ip, []);

        return forkJoin([removedAxeOs, ...swarmUpdate]);
      })
    ).subscribe({
      next: () => {
        this.toastr.success('Success!', 'Saved.');
        window.location.reload();
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
