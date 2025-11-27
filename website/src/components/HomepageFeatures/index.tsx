import type {ReactNode} from 'react';
import clsx from 'clsx';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

type FeatureItem = {
  title: string;
  icon: string;
  description: ReactNode;
};

const FeatureList: FeatureItem[] = [
  {
    title: 'Blazing Fast Performance',
    icon: '',
    description: (
      <>
        Built with modern C++ and a thread-per-core architecture, Titan delivers
        exceptional throughput and sub-millisecond latency. Designed to outperform
        Nginx and Pingora in production workloads.
      </>
    ),
  },
  {
    title: 'Zero-Downtime Reloads',
    icon: '',
    description: (
      <>
        Update routes, upstreams, and middleware without restarting. Hot configuration
        reloads use RCU (Read-Copy-Update) to ensure zero request failures during updates.
      </>
    ),
  },
  {
    title: 'Production Ready',
    icon: '',
    description: (
      <>
        Battle-tested with comprehensive health checks, connection pooling, and automatic
        error recovery. Full observability with metrics and structured logging built-in.
      </>
    ),
  },
];

function Feature({title, icon, description}: FeatureItem) {
  return (
    <div className={clsx('col col--4')}>
      {icon && (
        <div className="text--center">
          <div className={styles.featureIcon}>{icon}</div>
        </div>
      )}
      <div className="text--center padding-horiz--md">
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures(): ReactNode {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
