gnome = import('gnome')
sources = []

gdbus_headers = []

ifaces = [
  ['group-generated',        'org.group.', 'admin'],
  ['group-list-generated', 'org.group.admin.','list'],
]

foreach iface: ifaces
  gdbus_sources = gnome.gdbus_codegen(
    iface[0],
    join_paths(data_dir, iface[1] + iface[2] + '.xml'),
    interface_prefix: iface[1],
    namespace: 'UserGroup',
  )
  sources += gdbus_sources
  gdbus_headers += gdbus_sources[1]
endforeach

deps = [
  gio_dep,
  gio_unix_dep,
]

libaccounts_generated = static_library(
  'group-generated',
  sources: sources,
  include_directories: top_srcdir,
  dependencies: deps,
)

libaccounts_generated_dep = declare_dependency(
  sources: gdbus_headers,
  include_directories: include_directories('.'),
  dependencies: gio_dep,
  link_with: libaccounts_generated,
)

sources = files(
  'main.c',
  'group.c',
  'group-server.c',
  'util.c',
)

deps = [
  gio_unix_dep,
  glib_dep,
  libaccounts_generated_dep,
  polkit_gobject_dep,
]

executable(
  'group-admin-daemon',
  sources,
  include_directories: top_srcdir,
  dependencies: deps,
  install: true,
  install_dir: gas_libexecdir,
)

#subdir('libaccountsservice')
