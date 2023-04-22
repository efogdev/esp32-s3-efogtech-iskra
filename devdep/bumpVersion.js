const fs = require('fs');

const [ v0, v1, v2 ] = (fs.readFileSync('../.version').toString() || '0.0.0')
    .split('.').map(it => parseInt(it.trim()))

fs.writeFileSync('../.version', `${v0}.${v1}.${v2 + 1}`)
