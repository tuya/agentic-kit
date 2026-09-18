import React, {useEffect, useState} from 'react';
import Link from '@docusaurus/Link';
import Translate, {translate} from '@docusaurus/Translate';
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
      <div className="breadcrumb"><Link to="/"><Translate id="docs.article.breadcrumb.home" description="文档面包屑首页文字">TUYA AGENTIC-KIT</Translate></Link> / {shortTitle}</div>
      {!contentTitle && !frontMatter.hide_title && <h1>{metadata.title}</h1>}
      <MDXContent>{children}</MDXContent>
      <nav className="next" aria-label={translate({id: 'docs.article.pagination.ariaLabel', message: '相邻页面', description: '文档上一页和下一页导航的无障碍标签'})}>
        <Link to={metadata.previous?.permalink || '/'}><small>{metadata.previous ? <Translate id="docs.article.pagination.previous" description="文档上一页标签">上一页</Translate> : <Translate id="docs.article.pagination.return" description="无上一页时的返回标签">返回</Translate>}</small><strong>← {metadata.previous?.title || translate({id: 'docs.article.pagination.homeTitle', message: 'Agentic-kit 首页', description: '无上一页时的首页标题'})}</strong></Link>
        {metadata.next && <Link to={metadata.next.permalink}><small><Translate id="docs.article.pagination.next" description="文档下一页标签">下一步</Translate></small><strong>{metadata.next.title} →</strong></Link>}
      </nav>
      <footer className="doc-footer"><span><Translate id="docs.article.footer.copyright" description="文档页脚版权文字">© 2026 涂鸦智能</Translate></span><span><Translate id="docs.article.footer.product" description="文档页脚产品名称">Tuya Physical AI · Agentic-kit</Translate></span></footer>
    </main>
    <aside className="toc" aria-label={translate({id: 'docs.article.toc.ariaLabel', message: '本页目录', description: '文档页内目录的无障碍标签'})}><div className="toc-title"><Translate id="docs.article.toc.title" description="文档页内目录标题">本页总览</Translate></div>{!frontMatter.hide_table_of_contents && toc.map(item => <a key={item.id} className={`${item.level > 2 ? 'sub ' : ''}${activeId === item.id ? 'active' : ''}`} href={`#${item.id}`} dangerouslySetInnerHTML={{__html: tocLabels[item.id] || item.value}} />)}</aside>
  </>;
}
