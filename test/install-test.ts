import { downloadAddon, install } from '../src';

async function main() {
    await downloadAddon("/tmp/test/foo");
}

main();
