import React from 'react';
import renderRoutes from '@docusaurus/renderRoutes';
import LayoutProvider from '@theme/Layout/Provider';
import type {Props} from '@theme/DocVersionRoot';

export default function DocsRoot({route}: Props) {
  return <LayoutProvider><div className="reference-docs">{renderRoutes(route.routes!)}</div></LayoutProvider>;
}
