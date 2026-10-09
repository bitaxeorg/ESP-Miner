import { AddressPipe } from './address.pipe';

describe('AddressPipe', () => {
  let pipe: AddressPipe;

  beforeEach(() => {
    pipe = new AddressPipe();
  });

  it('create an instance', () => {
    expect(pipe).toBeTruthy();
  });

  it('should format a standalone Bitcoin address and wrap in font-mono span by default', () => {
    const result = pipe.transform('bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x');
    expect(result).toContain('<span class="font-mono">');
    expect(result).toContain('bc1q');
    expect(result).toContain('...');
    expect(result).toContain('</span>');
  });

  it('should format a standalone Bitcoin address as plain text when html is false', () => {
    const result = pipe.transform('bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x', { html: false });
    expect(result).not.toContain('<span');
    expect(result).toContain('bc1q');
    expect(result).toContain('...');
  });

  it('should preserve account usernames without any address formatting or font-mono spans', () => {
    const result = pipe.transform('satoshi.worker1');
    expect(result).toBe('satoshi.worker1');
    expect(result).not.toContain('<span');
  });

  it('should preserve SRI solo prefix and worker suffix while formatting and wrapping the address', () => {
    const result = pipe.transform('sri/solo/bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x/worker1');
    expect(result).toContain('sri/solo/<span class="font-mono">');
    expect(result).toContain('bc1q');
    expect(result).toContain('...');
    expect(result).toContain('</span>/worker1');
  });

  it('should preserve SRI donate prefix and worker suffix while formatting and wrapping the address', () => {
    const result = pipe.transform('sri/donate/10/bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x/worker1');
    expect(result).toContain('sri/donate/10/<span class="font-mono">');
    expect(result).toContain('...');
    expect(result).toContain('</span>/worker1');
  });

  it('should preserve SRI full donate worker without address', () => {
    const result = pipe.transform('sri/donate/worker1');
    expect(result).toBe('sri/donate/worker1');
  });

  it('should format and wrap multiple addresses with delimiters and worker suffix', () => {
    const result = pipe.transform('bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x, 1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV.worker1');
    expect(result).toContain('<span class="font-mono">');
    expect(result).toContain(', <span class="font-mono">');
    expect(result).toContain('</span>.worker1');
  });

  it('should correctly detect if a string contains a Bitcoin payout address', () => {
    expect(AddressPipe.hasAddress('bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x')).toBeTrue();
    expect(AddressPipe.hasAddress('bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.worker1')).toBeTrue();
    expect(AddressPipe.hasAddress('sri/solo/bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x/worker1')).toBeTrue();
    expect(AddressPipe.hasAddress('sri/donate/10/bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x/worker1')).toBeTrue();
    expect(AddressPipe.hasAddress('1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV')).toBeTrue();
    expect(AddressPipe.hasAddress('satoshi.worker1')).toBeFalse();
    expect(AddressPipe.hasAddress('sri/donate/worker1')).toBeFalse();
    expect(AddressPipe.hasAddress('')).toBeFalse();
    expect(AddressPipe.hasAddress(null as any)).toBeFalse();
  });
});
