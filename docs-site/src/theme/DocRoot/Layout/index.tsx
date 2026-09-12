import React from 'react';
import type {Props} from '@theme/DocRoot/Layout';
import {Topbar, Sidebar} from './Chrome';

export default function DocRootLayout({children}: Props) {
  return (
    <>
      <Topbar />
      <div className="docs-shell">
        <Sidebar />
        {children}
      </div>
    </>
  );
}
