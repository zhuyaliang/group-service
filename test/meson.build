testprg = executable('tests',
  sources : 'main.c',
  dependencies : [glib_dep],
  link_with: libgroupservice,
  include_directories: [top_srcdir, src_subdir],
  )

test('test1', testprg)
