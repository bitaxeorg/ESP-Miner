import { HttpErrorResponse } from '@angular/common/http';

export function getHttpErrorMessage(err: any, uri?: string): string {
  let message = 'An unknown error occurred.';

  if (err instanceof HttpErrorResponse) {
    if (err.status === 0) {
      message = 'Network error or connection lost. The device may have restarted or disconnected.';
    } else if (err.status === 403 && err.headers && err.headers.get('x-ratelimit-reset')) {
      const resetHeader = err.headers.get('x-ratelimit-reset');
      const resetEpoch = parseInt(resetHeader!, 10);
      let resetInfo = '';
      if (!isNaN(resetEpoch)) {
        const resetDate = new Date(resetEpoch * 1000);
        const timeString = resetDate.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        const diffMinutes = Math.ceil((resetDate.getTime() - Date.now()) / 60000);
        resetInfo = diffMinutes > 0
          ? ` Rate limit resets at ${timeString} (in ~${diffMinutes} min).`
          : ` Rate limit resets at ${timeString}.`;
      }
      const rawMsg = (typeof err.error === 'object' && err.error?.message) ? err.error.message : (err.message || 'API rate limit exceeded.');
      message = rawMsg + resetInfo;
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
