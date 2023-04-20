import path from 'path'
import fs from 'fs/promises'

(async () => {
    const currentPath = process.cwd()

    const html = await fs.readFile(path.join(currentPath, 'build', 'index.html'))
    const css = await fs.readFile(path.join(currentPath, 'build', 'style.css'))

    const monsterFile = html.toString('utf8')
        .replaceAll(`<link href="/style.css" rel="stylesheet">`, `<style>${css.toString('utf8')}</style>`)

    await fs.writeFile(path.join(currentPath, '..', 'ui-bundle', 'index.html'), monsterFile)

    console.log('Bundling done')
})()
