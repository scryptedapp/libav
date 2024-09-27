import { downloadAddon } from '../src';

async function main() {
    await downloadAddon("/tmp/test/foo");
}

main();
