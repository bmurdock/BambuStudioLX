/** @webview-only */
import { test, expect, type Page } from '@playwright/test';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

type Packet = {
  head: { version: string; type: string; seq: number; ts: number }
  body: Record<string, unknown>
}
type BridgeApi = { requestA?: (action: string) => Promise<unknown>; requestB?: (action: string) => Promise<unknown>; unmountA?: () => void }

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEVICE_PAGE_DIR = process.env.DEVICE_PAGE_DIR ?? path.resolve(HERE, '../../../src/slic3r/GUI/DeviceWeb/device_page');
const FIXTURE = path.join(DEVICE_PAGE_DIR, 'tests/bridgeIsolationFixture.tsx');

async function buildFixture(): Promise<string> {
  const requireFromDevicePage = createRequire(path.join(DEVICE_PAGE_DIR, 'package.json'));
  const vite = await import(requireFromDevicePage.resolve('vite')) as {
    build: (options: Record<string, unknown>) => Promise<Array<{ output: Array<{ type: string; code?: string }> }>>
  };
  const result = await vite.build({
    root: DEVICE_PAGE_DIR,
    configFile: false,
    logLevel: 'error',
    define: { 'process.env.NODE_ENV': JSON.stringify('production') },
    build: { write: false, lib: { entry: FIXTURE, name: 'BridgeIsolationFixture', formats: ['iife'] }, rollupOptions: { output: { inlineDynamicImports: true } } },
  });
  const chunk = result.flatMap(bundle => bundle.output).find(item => item.type === 'chunk' && item.code);
  if (!chunk?.code) throw new Error('Vite did not produce a bridge isolation fixture bundle');
  return chunk.code;
}

async function respond(page: Page, packet: Packet, body: Record<string, unknown>) {
  await page.evaluate(({ packet, body }) => {
    document.dispatchEvent(new CustomEvent('cpp:device', { detail: { head: { ...packet.head, type: 'response', ts: Date.now() }, body } }));
  }, { packet, body });
}

test('bridge instances isolate interleaved responses and lifecycle cancellation @webview-only', async ({ page }) => {
  const bundle = await buildFixture();
  await page.goto('about:blank');
  await page.evaluate(() => {
    const win = window as Window & { __bridgePackets?: Packet[]; chrome?: { webview?: { postMessage?: (message: string) => void } } };
    win.__bridgePackets = [];
    win.chrome = { webview: { postMessage: message => win.__bridgePackets?.push(JSON.parse(message) as Packet) } };
  });
  await page.addScriptTag({ content: bundle });
  await expect.poll(() => page.evaluate(() => {
    const api = (window as unknown as { __bridgeTest?: BridgeApi }).__bridgeTest;
    return Boolean(api?.requestA && api.requestB);
  })).toBe(true);

  const requestA = page.evaluate(() => (window as unknown as { __bridgeTest: BridgeApi }).__bridgeTest.requestA!('first'));
  const requestB = page.evaluate(() => (window as unknown as { __bridgeTest: BridgeApi }).__bridgeTest.requestB!('second'));
  await expect.poll(() => page.evaluate(() => (window as unknown as { __bridgePackets?: Packet[] }).__bridgePackets?.length ?? 0)).toBe(2);
  const packets = await page.evaluate(() => (window as unknown as { __bridgePackets: Packet[] }).__bridgePackets.slice());
  expect(new Set(packets.map(packet => packet.head.seq)).size).toBe(2);
  await respond(page, packets[1]!, { module: 'bridge-test', action: 'second', error_code: 0 });
  await respond(page, packets[0]!, { module: 'bridge-test', action: 'first', error_code: 0 });
  await expect(requestA).resolves.toMatchObject({ ok: true, value: { action: 'first' } });
  await expect(requestB).resolves.toMatchObject({ ok: true, value: { action: 'second' } });

  await page.evaluate(() => {
    const win = window as unknown as Window & { __bridgePackets?: Packet[]; chrome: { webview: { postMessage: (message: string) => void } } };
    const original = win.chrome.webview.postMessage;
    win.chrome.webview.postMessage = message => {
      const packet = JSON.parse(message) as Packet;
      (win as unknown as { __bridgePackets: Packet[] }).__bridgePackets.push(packet);
      document.dispatchEvent(new CustomEvent('cpp:device', { detail: { head: { ...packet.head, type: 'response', ts: Date.now() }, body: { module: 'bridge-test', error_code: 0 } } }));
      win.chrome.webview.postMessage = original;
    };
  });
  await expect(page.evaluate(() => (window as unknown as { __bridgeTest: BridgeApi }).__bridgeTest.requestB!('synchronous'))).resolves.toMatchObject({ ok: true });

  const oldRequest = page.evaluate(() => (window as unknown as { __bridgeTest: BridgeApi }).__bridgeTest.requestA!('canceled'));
  await expect.poll(() => page.evaluate(() => (window as unknown as { __bridgePackets?: Packet[] }).__bridgePackets?.length ?? 0)).toBe(4);
  const oldPacket = await page.evaluate(() => (window as unknown as { __bridgePackets: Packet[] }).__bridgePackets[3]!);
  await page.evaluate(() => (window as unknown as { __bridgeTest: BridgeApi }).__bridgeTest.unmountA!());
  await expect(oldRequest).resolves.toEqual({ ok: false, error: 'Request Canceled' });

  const newRequest = page.evaluate(() => (window as unknown as { __bridgeTest: BridgeApi }).__bridgeTest.requestB!('after-unmount'));
  await expect.poll(() => page.evaluate(() => (window as unknown as { __bridgePackets?: Packet[] }).__bridgePackets?.length ?? 0)).toBe(5);
  const newPacket = await page.evaluate(() => (window as unknown as { __bridgePackets: Packet[] }).__bridgePackets[4]!);
  expect(newPacket.head.seq).not.toBe(oldPacket.head.seq);
  await respond(page, oldPacket, { module: 'bridge-test', action: 'canceled', error_code: 0 });
  await expect(Promise.race([newRequest.then(() => 'settled'), new Promise(resolve => setTimeout(() => resolve('pending'), 100))])).resolves.toBe('pending');
  await respond(page, newPacket, { module: 'bridge-test', action: 'after-unmount', error_code: 0 });
  await expect(newRequest).resolves.toMatchObject({ ok: true });
});
