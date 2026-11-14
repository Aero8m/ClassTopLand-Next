#pragma once
#include <QJsonArray>
#include <QJsonObject>

enum class AgentToolAccess { Always, PluginAction, PluginWrite, WorkspaceRead, WorkspaceWrite, Launch, Command };
struct AgentToolDefinition { AgentToolAccess access; QJsonObject schema; };
const QList<AgentToolDefinition> &agentToolDefinitions();
bool validAgentArguments(const QJsonArray &schemas, const QString &name, const QJsonObject &args);
