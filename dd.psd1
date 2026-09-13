@{
    schema = 1
    project = @{
        name = 'noterad'
        type = 'gui'
        'default-target' = 'app'
    }
    dependencies = @{ owner = 'dd' }
    build = @{
        'x64-windows' = @{
            debug = 'debug'
            release = 'release'
        }
    }
    targets = @(
        @{
            id = 'app'
            kind = 'gui'
            'cmake-target' = 'rethinkify'
            'test-label' = 'noterad'
            'debug-path' = 'exe/rethinkify-64d{exe}'
            'release-path' = 'exe/rethinkify-64{exe}'
            platforms = @('x64-windows')
        }
    )
}
