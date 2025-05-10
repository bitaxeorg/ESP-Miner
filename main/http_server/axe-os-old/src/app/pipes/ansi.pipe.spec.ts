import { ANSIPipe } from './ansi.pipe';

describe('ANSIPipe', () => {
  it('create an instance', () => {
    const pipe = new ANSIPipe();
    expect(pipe).toBeTruthy();
  });
});
