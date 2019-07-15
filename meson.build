project('exotracker', 'cpp', default_options : [
    'cpp_std=c++17'
])

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: ['Core', 'Gui', 'Widgets'])

inc = include_directories('src')

moc_files = qt5.preprocess(
    # moc_extra_arguments: ['-DMAKES_MY_MOC_HEADER_COMPILE'],
    include_directories: inc,
    dependencies: qt5_dep,

    moc_headers : 'src/mainwindow.h',
)
# translations = qt5.compile_translations(ts_files : 'myTranslation_fr.ts', build_by_default : true)

executable('exotracker',
    moc_files,

    'src/main.cpp',
    'src/mainwindow.cpp',
    'src/mainwindow.h',

    'src/gui/layout_stack.cpp',
    'src/gui/layout_stack.h',

    include_directories: inc,
    dependencies : qt5_dep,
)
