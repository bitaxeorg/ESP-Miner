import { Component } from '@angular/core';
import { DynamicDialogConfig, DynamicDialogRef } from 'primeng/dynamicdialog';

@Component({
  selector: 'app-login-modal',
  templateUrl: './login-modal.component.html',
  standalone: false
})
export class LoginModalComponent {
  public username = 'admin';
  public password = '';

  constructor(
    public ref: DynamicDialogRef,
    public config: DynamicDialogConfig
  ) {}

  public onCancel(): void {
    this.ref.close(null);
  }

  public onSubmit(): void {
    this.ref.close({
      username: this.username,
      password: this.password
    });
  }
}
