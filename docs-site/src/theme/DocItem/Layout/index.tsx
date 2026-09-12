import React, {useEffect, useState} from 'react';
import Link from '@docusaurus/Link';
import {useDoc} from '@docusaurus/plugin-content-docs/client';
import MDXContent from '@theme/MDXContent';
import type {Props} from '@theme/DocItem/Layout';

// The topbar and sidebar live in DocsRoot (above the route), so a doc-page
// navigation only swaps this article + TOC — the sidebar never remounts and
// keeps its scroll position.
export default function DocItemLayout({children}: Props) {
  const {metadata, toc, contentTitle, frontMatter} = useDoc();
  const [activeId, setActiveId] = useState('');
  useEffect(() => {
    setActiveId('');
    const observer = new IntersectionObserver(entries => {
      const visible = entries.filter(e => e.isIntersecting).sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top)[0];
      if (visible) setActiveId(visible.target.id);
    }, {rootMargin: '-95px 0px -70% 0px', threshold: [0, 1]});
    toc.forEach(item => { const target = document.getElementById(item.id); if (target) observer.observe(target); });
    return () => observer.disconnect();
  }, [metadata.id, toc]);
  const tocLabels = (frontMatter as typeof frontMatter & {toc_labels?: Record<string, string>}).toc_labels || {};
  const shortTitle = frontMatter.sidebar_label || metadata.title;
  return <>
    <main className="article" id="content">
      <div className="breadcrumb"><Link to="/">TUYA AGENTIC-KIT</Link> / {shortTitle}</div>
      {!contentTitle && !frontMatter.hide_title && <h1>{metadata.title}</h1>}
      <MDXContent>{children}</MDXContent>
      <nav className="next" aria-label="相邻页面">
        <Link to={metadata.previous?.permalink || '/'}><small>{metadata.previous ? '上一页' : '返回'}</small><strong>← {metadata.previous?.title || 'Agentic-kit 首页'}</strong></Link>
        {metadata.next && <Link to={metadata.next.permalink}><small>下一步</small><strong>{metadata.next.title} →</strong></Link>}
      </nav>
      <footer className="doc-footer"><span>© 2026 Tuya Inc.</span><span>Tuya Physical AI · Agentic-kit</span></footer>
    </main>
    <aside className="toc" aria-label="本页目录"><div className="toc-title">本页总览</div>{!frontMatter.hide_table_of_contents && toc.map(item => <a key={item.id} className={`${item.level > 2 ? 'sub ' : ''}${activeId === item.id ? 'active' : ''}`} href={`#${item.id}`} dangerouslySetInnerHTML={{__html: tocLabels[item.id] || item.value}} />)}</aside>
  </>;
}
