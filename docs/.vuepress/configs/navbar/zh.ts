import type { NavbarConfig } from "@vuepress/theme-default";

export const zh: NavbarConfig = [
  {
    text: "部署指南",
    children: [
      "/zh/deploying/introduction.html",
      "/zh/deploying/quick-start.html",
      "/zh/deploying/deploy.html",
      {
        text: "存储设备的准备",
        children: [
          "/zh/deploying/storage-aliyun-essd.html",
          "/zh/deploying/storage-curvebs.html",
          "/zh/deploying/storage-ceph.html",
          "/zh/deploying/storage-nbd.html",
        ],
      },
      {
        text: "文件系统的准备",
        children: [
          "/zh/deploying/fs-pfs.html",
          "/zh/deploying/fs-pfs-curve.html",
        ],
      },
      {
        text: "编译部署 PolarDB 内核",
        children: [
          "/zh/deploying/db-localfs.html",
          "/zh/deploying/db-pfs.html",
        ],
      },
      {
        text: "更多部署方式",
        children: [
          "/zh/deploying/deploy-stack.html",
          "/zh/deploying/deploy-official.html",
        ],
      },
    ],
  },
  {
    text: "使用与运维",
    link: "/zh/operation/",
    children: [
      {
        text: "日常运维",
        children: ["/zh/operation/backup-and-restore.html"],
      },
      {
        text: "性能测试",
        children: ["/zh/operation/tpcc-test.html"],
      },
    ],
  },
  {
    text: "特性实践",
    link: "/zh/features/",
    children: [
      {
        text: "HTAP",
        children: ["/zh/features/tpch-on-px.html"],
      },
    ],
  },
  {
    text: "原理解读",
    link: "/zh/theory/",
    children: [
      {
        text: "PolarDB for PostgreSQL",
        children: [
          "/zh/theory/arch-overview.html",
          "/zh/theory/buffer-management.html",
          "/zh/theory/ddl-synchronization.html",
          "/zh/theory/logindex.html",
        ],
      },
      {
        text: "PostgreSQL",
        children: ["/zh/theory/analyze.html"],
      },
    ],
  },
  {
    text: "上手开发",
    link: "/zh/development/",
    children: [
      "/zh/development/dev-on-docker.html",
      "/zh/development/customize-dev-env.html",
    ],
  },
  {
    text: "参与社区",
    link: "/zh/contributing/",
    children: [
      {
        text: "行为准则",
        link: "/zh/contributing/code-of-conduct.html",
      },
      {
        text: "贡献文档",
        link: "/zh/contributing/contributing-polardb-docs.html",
      },
      {
        text: "贡献代码",
        link: "/zh/contributing/contributing-polardb-kernel.html",
      },
      {
        text: "编码风格",
        link: "/zh/contributing/coding-style.html",
      },
    ],
  },
];
