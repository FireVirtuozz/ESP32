package tg.windowscontrols.ws;

import java.net.URI;

import tg.windowscontrols.ws.endpoint.ClientEndpointEsp;

public class WsClient {

    public WsClient() {}
    
    public void connect(final String WsUrl) {
        ClientEndpointEsp clientEndEsp = new ClientEndpointEsp(URI.create(WsUrl));
    }
}
