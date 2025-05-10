import { Injectable } from '@angular/core';

@Injectable({
  providedIn: 'root'
})
export class LocalStorageService {

  constructor() { }

  setItem(key: string, value: any) {
    localStorage.setItem(key, value);
  }

  getItem(key: string): any {
    return localStorage.getItem(key);
  }

  setBool(key: string, value: boolean) {
    localStorage.setItem(key, String(value));
  }

  getBool(key: string): boolean {
    return localStorage.getItem(key) === 'true';
  }

  setObject(key: string, value: object) {
    localStorage.setItem(key, JSON.stringify(value));
  }

  getObject(key: string): any  | null{
    const item = localStorage.getItem(key);
    if(item == null || item.length < 1){
      return null;
    }
    return JSON.parse(item);
  }

  setNumber(key: string, value: number) {
    localStorage.setItem(key, value.toString());
  }

  getNumber(key: string): number | null {
    const value = localStorage.getItem(key);
    return value ? Number(value) : null;
  }
}
