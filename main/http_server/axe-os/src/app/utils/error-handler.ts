import { HttpErrorResponse } from '@angular/common/http';

export function getHttpErrorMessage(err: any, uri?: string): string {
  let message = 'An unknown error occurred.';

  if (err instanceof HttpErrorResponse) {
    if (err.status === 0) {
      message = 'Network error or connection lost. The device may have restarted or disconnected.';
    } else if (err.error) {
      if (typeof err.error === 'string') {
        message = err.error;
      } else if (typeof err.error === 'object') {
        if (err.error.message) {
          message = err.error.message;
        } else if (err.error instanceof ProgressEvent) {
          message = 'Upload failed: network error or connection closed.';
        }
      }
    } else {
      message = err.message || err.statusText || message;
    }
  } else if (err instanceof Error) {
    message = err.message;
  } else if (typeof err === 'string') {
    message = err;
  }

  if (uri) {
    return `${message} (Device: ${uri})`;
  }
  return message;
}
