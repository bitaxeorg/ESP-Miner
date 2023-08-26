import { Component } from '@angular/core';
import { ToastrService } from 'ngx-toastr';
import { Observable } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent {

  public info$: Observable<any>;

  constructor(
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {
    this.info$ = this.systemService.getInfo().pipe(
      this.loadingService.lockUIUntilComplete()
    )

  }

  public restart() {
    this.systemService.restart().subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }
}
