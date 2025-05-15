import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'dateAgo',
  pure: true,
  standalone: false
})
export class DateAgoPipe implements PipeTransform {

  transform(value: any, args?: any): any {
    if (value) {
      value = new Date().getTime() - value * 1000;
      let seconds = Math.floor((+new Date() - +new Date(value)) / 1000);
      if (seconds < 29) // less than 30 seconds ago will show as 'Just now'
        return 'Just now';
      const intervals: { [key: string]: number } = {
        'year': 31536000,
        'month': 2592000,
        'week': 604800,
        'day': 86400,
        'hour': 3600,
        'minute': 60,
        'second': 1
      };
      let result = '';
      let shownIntervals = 0;
      for (const i in intervals) {
        if (args?.intervals && shownIntervals >= args.intervals) break;
        const counter = Math.floor(seconds / intervals[i]);
        if (counter > 0) {
          if (counter === 1) {
            if (result) result += ', '
            result += counter + ' ' + i + ''; // singular (1 day ago)
            seconds -= intervals[i]
          } else {
            if (result) result += ', '
            result += counter + ' ' + i + 's'; // plural (2 days ago)
            seconds -= intervals[i] * counter
          }
          shownIntervals++;
        }
      }
      return result;
    }
    return value;
  }

}
