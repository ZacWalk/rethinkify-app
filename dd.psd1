@{
    schema = 1
    project = @{
        name = 'rethinkify'
        type = 'gui'
        'default-target' = 'rethinkify'
    }
    dependencies = @{
        owner = 'application'
    }
    build = @{
        'x64-windows' = @{
            debug = 'debug'
            release = 'release'
        }
    }
    targets = @(
        @{
            id = 'rethinkify'
            kind = 'gui'
            'cmake-target' = 'rethinkify'
            'test-label' = 'rethinkify'
            'debug-path' = 'exe/rethinkify-64d.exe'
            'release-path' = 'exe/rethinkify-64.exe'
            platforms = @('x64-windows')
        }
    )
}
