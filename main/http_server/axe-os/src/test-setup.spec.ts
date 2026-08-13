beforeEach(() => {
  spyOn(console, 'error').and.callThrough();
});

afterEach(() => {
  expect(console.error).not.toHaveBeenCalled();
});
