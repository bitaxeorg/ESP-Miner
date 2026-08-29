import { DiffSuffixPipe } from './diff-suffix.pipe';

describe('DiffSuffixPipe', () => {
  const pipe = new DiffSuffixPipe();

  it('create an instance', () => {
    expect(pipe).toBeTruthy();
  });

  it('should format with default 2 digits', () => {
    expect(pipe.transform(1000)).toBe('1.00 K');
    expect(pipe.transform(1000000)).toBe('1.00 M');
  });

  it('should format with custom digits parameter', () => {
    expect(pipe.transform(1000, 0)).toBe('1 K');
    expect(pipe.transform(1000000, 0)).toBe('1 M');
    expect(pipe.transform(1500000, 1)).toBe('1.5 M');
    expect(pipe.transform(1500000, 2)).toBe('1.50 M');
  });
});
