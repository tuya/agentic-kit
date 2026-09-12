import React from 'react';
import Original from '@theme-original/MDXComponents';
import type {MDXComponentsObject} from '@theme/MDXComponents';

const components: MDXComponentsObject = {
  ...Original,
  table: props => <div className="table-wrap"><table {...props} /></div>,
};
export default components;
