import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';

import { useDeviceBridge } from '../src/hooks/Bridge';

type BridgeTestApi = {
  requestA?: (action: string) => Promise<unknown>
  requestB?: (action: string) => Promise<unknown>
  unmountA?: () => void
}

declare global {
  interface Window {
    __bridgeTest?: BridgeTestApi
  }
}

export function Client({ name }: { name: 'requestA' | 'requestB' }) {
  const request = useDeviceBridge();

  useEffect(() => {
    window.__bridgeTest ??= {};
    window.__bridgeTest[name] = (action: string) => request({
      module: 'bridge-test',
      action,
      payload: { client: name },
    });
    return () => {
      if (window.__bridgeTest?.[name]) delete window.__bridgeTest[name];
    };
  }, [name, request]);

  return React.createElement('div', { 'data-client': name });
}

export function BridgeIsolationFixture() {
  const [showA, setShowA] = useState(true);

  useEffect(() => {
    window.__bridgeTest ??= {};
    window.__bridgeTest.unmountA = () => setShowA(false);
    return () => { delete window.__bridgeTest; };
  }, []);

  return React.createElement(
    React.Fragment,
    null,
    showA ? React.createElement(Client, { name: 'requestA' }) : null,
    React.createElement(Client, { name: 'requestB' }),
  );
}

const root = document.createElement('div');
root.id = 'bridge-isolation-root';
document.body.appendChild(root);
createRoot(root).render(React.createElement(BridgeIsolationFixture));
