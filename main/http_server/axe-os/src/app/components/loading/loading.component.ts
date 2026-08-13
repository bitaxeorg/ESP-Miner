import { Component } from '@angular/core';
import { Observable } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { AsyncPipe } from '@angular/common';

@Component({
  selector: 'app-loading',
  templateUrl: './loading.component.html',
  styleUrls: ['./loading.component.scss'],
  imports: [AsyncPipe]
})
export class LoadingComponent {

  public loading$: Observable<boolean>;

  constructor(private loadingService: LoadingService) {
    this.loading$ = this.loadingService.loading$.asObservable();
  }
}
