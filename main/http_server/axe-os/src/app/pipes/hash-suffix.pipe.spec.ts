import { HashSuffixPipe } from './hash-suffix.pipe';

describe('HashSuffixPipe', () => {
  let pipe: HashSuffixPipe;

  beforeEach(() => {
    pipe = new HashSuffixPipe();
  });

  it('create an instance', () => {
    expect(pipe).toBeTruthy();
  });

  it('formats standard hashrate with suffix', () => {
    expect(pipe.transform(500)).toBe('500 Gh/s');
    expect(pipe.transform(1200)).toBe('1.20 Th/s');
    expect(pipe.transform(0.05)).toBe('50.0 Mh/s');
    expect(pipe.transform(0)).toBe('0 H/s');
  });

  it('hides unit when hideUnit is true', () => {
    expect(pipe.transform(500, { hideUnit: true })).toBe('500');
    expect(pipe.transform(62.5, { hideUnit: true })).toBe('62.5');
    expect(pipe.transform(0, { hideUnit: true })).toBe('0');
  });

  it('scales by fixed power when provided', () => {
    expect(pipe.transform(500, { power: 3, hideUnit: true })).toBe('500');
    expect(pipe.transform(0.05, { power: 3, hideUnit: true })).toBe('0.05');
  });

  it('computes correct power and suffix from static helpers', () => {
    expect(HashSuffixPipe.getPower(500)).toBe(3);
    expect(HashSuffixPipe.getPower(1200)).toBe(4);
    expect(HashSuffixPipe.getSuffix(3)).toBe('Gh/s');
    expect(HashSuffixPipe.getSuffix(4)).toBe('Th/s');
  });
});
