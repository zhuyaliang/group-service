pkg = import('pkgconfig')
libgroupsservice_dir = meson.current_source_dir()

#subdir = gas_id.to_lower()
subdir = 'libgroupservice'

headers = files(
  'gas-group.h',
  'gas-group-manager.h',
)

install_headers(
  headers + ['gas.h'],
  install_dir: join_paths(gas_pkgincludedir, subdir),
)

sources = files(
  'gas-group.c',
  'gas-group-manager.c',
)

enum_types = 'gas-group-enum-types'

enum_sources = gnome.mkenums(
  enum_types,
  sources: headers,
  c_template: enum_types + '.c.template',
  h_template: enum_types + '.h.template',
  install_header: true,
  install_dir: join_paths(gas_pkgincludedir, subdir),
)

dbus_sources = []

deps = [
  crypt_dep,
  gio_unix_dep,
  glib_dep,
  libaccounts_generated_dep,
]

symbol_map = join_paths(meson.current_source_dir(), 'symbol.map')
ldflags = cc.get_supported_link_arguments('-Wl,--version-script,@0@'.format(symbol_map))

libgroupservice = shared_library(
  gas_name,
  sources: sources + enum_sources + dbus_sources,
  version: libversion,
  include_directories: top_srcdir,
  dependencies: deps,
  c_args: '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name()),
  link_args: ldflags,
  link_depends: symbol_map,
  install: true,
)

libaccountsservice_dep = declare_dependency(
  sources: enum_sources[1],
  include_directories: include_directories('.'),
  dependencies: [gio_dep, glib_dep],
  link_with: libgroupservice,
)

pkg.generate(
  libgroupservice,
  name: 'Group Service',
  description: 'Client Library for communicating with group service',
  filebase: gas_name,
  subdirs: gas_api_name,
  variables: [
    'exec_prefix=${prefix}',
    'datadir=${prefix}/' + get_option('datadir'),
    'interfacesdir=${datadir}/group-service/interfaces',
  ],
)

#if get_option('introspection')
#  gnome.generate_gir(
#    libgroupservice,
#    sources: sources + headers + [enum_sources[1]],
#    includes: ['GObject-2.0', 'Gio-2.0'],
#    dependencies: libaccountsservice_dep,
#    namespace: 'Group Service',
#    nsversion: gas_api_version,
#    identifier_prefix: gas_id,
#    header: join_paths(subdir, 'gas.h'),
#    install: true,
#  )
#endif
