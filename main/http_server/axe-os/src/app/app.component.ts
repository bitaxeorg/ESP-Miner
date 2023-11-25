import { Component } from '@angular/core';
import { ToastrService } from 'ngx-toastr';

import { SystemService } from './services/system.service';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss']
})
export class AppComponent {
  constructor(
    private systemService: SystemService,
    private toastr: ToastrService,
  ) {


  }

  public restart() {
    this.systemService.restart().subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }


}
