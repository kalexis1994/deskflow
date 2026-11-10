import { Fragment, type ReactNode } from "react";
import { t, type Key } from "./index";

/**
 * A text with elements inside, such as a name in bold: `{name}` placeholders take
 * nodes, so each language can put them where its grammar wants.
 */
export function tNode(key: Key, nodes: Record<string, ReactNode>): ReactNode {
  const parts = t(key).split(/(\{\w+\})/);
  return parts.map((part, i) => {
    const name = part.match(/^\{(\w+)\}$/)?.[1];
    return <Fragment key={i}>{name !== undefined && name in nodes ? nodes[name] : part}</Fragment>;
  });
}
