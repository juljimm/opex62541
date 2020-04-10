defmodule CClientLifecycleTest do
  use ExUnit.Case
  doctest Opex62541

  #alias OpcUA.{NodeId, Server, QualifiedName}
  alias OpcUA.Client

  setup do
    {:ok, pid} = Client.start_link()
    %{pid: pid}
  end

  test "Get client state", state do
    c_response = Client.get_state(state.pid)
    assert c_response == {:ok, "Disconnected"}
  end

  test "Set/Get client config", state do
    config = %{
      "requestedSessionTimeout" => 12000,
      "secureChannelLifeTime" => 6000,
      "timeout" => 500
    }

    c_response = Client.set_config(state.pid, config)
    assert c_response == :ok

    c_response = Client.get_config(state.pid)
    assert c_response == {:ok, config}
  end

  test "Reset client", state do
    c_response = Client.reset(state.pid)
    assert c_response == :ok
  end
end

