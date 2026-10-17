install_data('gi/overrides/IBus.py', install_dir: pyoverridesdir)

pyoverrides_tests_env = {
  'PYTHONPATH': '@0@:@1@'.format(
          source_root / 'tests',
          python.get_variable('PYTHONPATH', '')),
  'PYTHONMALLOC': 'malloc',
  'LD_LIBRARY_PATH': build_root / 'src',
  'GI_TYPELIB_PATH': build_root / 'src',
  'IBUS_KEYMAP_PATH': source_root / 'data' / 'keymaps',
}

if get_option('tests')
  pyoverrides_tests = [
    {
      'name': 'test-deserialize',
    },
    {
      'name': 'test-override-ibus',
    },
  ]

  foreach _test : pyoverrides_tests
    timeout = _test.get('timeout', 30)
    args =  files([python.full_path(), '@0@.py'.format(_test['name'])])
    test(_test['name'], ibus_tests_runner,
      args: args,
      env: environment(pyoverrides_tests_env + ibus_tests_env),
      suite: 'bindings/pygobject',
      timeout: timeout,
    )
  endforeach
endif
